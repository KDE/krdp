// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// This file is roughly based on grd-session-rdp.c from gnome-remote-desktop,
// which is:
//
// SPDX-FileCopyrightText: 2020-2023 Pascal Nowack
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "RdpConnection.h"

#include <filesystem>
#include <optional>

#include <fcntl.h>

#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QThread>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/freerdp.h>

#ifdef FREERDP3
#include <freerdp/channels/drdynvc.h>
#else
#define DRDYNVC_SVC_CHANNEL_NAME "drdynvc"
#endif

#include "Cursor.h"
#include "InputHandler.h"
#include "NetworkDetection.h"
#include "PeerContext_p.h"
#include "Server.h"
#include "VideoStream.h"

#include "krdp_logging.h"

namespace fs = std::filesystem;

namespace KRdp
{

/**
 * Create the "sam" file used by FreeRDP for reading username and password
 * information. It hashes the password in the appropriate format and writes that
 * along with the username to the provided temporary file.
 */
bool createSamFile(QTemporaryFile &file, const QList<User> &users)
{
    auto runtimePath = fs::path(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation).toStdString());

    auto path = runtimePath / "krdp";
    fs::create_directories(path);

    file.setFileTemplate(QString::fromStdString(path / "rdp-sam-XXXXXX"));
    if (!file.open()) {
        qCWarning(KRDP) << "Could not open SAM file";
        return false;
    }

    QString data;

    for (const auto &user : users) {
        auto username = user.name;
        auto password = user.password.toUtf8();

        std::array<uint8_t, 16> hash;
        NTOWFv1A((LPSTR)password.data(), password.size(), hash.data());

        auto entry = QStringLiteral("%1:::").arg(username);
        for (int i = 0; i < 16; ++i) {
            entry.append(QStringLiteral("%1").arg(hash[i], 2, 16, QLatin1Char('0')));
        }
        entry.append(QStringLiteral(":::\n"));
        data.append(entry);
    }

    file.write(data.toUtf8());
    file.close();

    return true;
}

/**
 * FreeRDP callback for the capabilities event.
 */
BOOL peerCapabilities(freerdp_peer *peer)
{
    auto context = reinterpret_cast<PeerContext *>(peer->context);
    if (context->connection->onCapabilities()) {
        return TRUE;
    }

    return FALSE;
}

/**
 * FreeRDP callback for the post connect event.
 */
BOOL peerPostConnect(freerdp_peer *peer)
{
    auto context = reinterpret_cast<PeerContext *>(peer->context);
    if (context->connection->onPostConnect()) {
        return TRUE;
    }

    return FALSE;
}

/**
 * FreeRDP callback for the activate event.
 */
BOOL peerActivate(freerdp_peer *peer)
{
    auto context = reinterpret_cast<PeerContext *>(peer->context);
    if (context->connection->onActivate()) {
        return TRUE;
    }

    return FALSE;
}

BOOL suppressOutput(rdpContext *context, uint8_t allow, const RECTANGLE_16 *)
{
    auto peerContext = reinterpret_cast<PeerContext *>(context);
    if (peerContext->connection->onSuppressOutput(allow)) {
        return TRUE;
    }

    return FALSE;
}

class KRDP_NO_EXPORT RdpConnection::Private
{
public:
    Server *server = nullptr;

    State state = State::Initial;

    qintptr socketHandle;

    std::unique_ptr<InputHandler> inputHandler;
    std::unique_ptr<VideoStream> videoStream;
    std::unique_ptr<Cursor> cursor;
    std::unique_ptr<NetworkDetection> networkDetection;

    freerdp_peer *peer = nullptr;

    std::jthread thread;

    QTemporaryFile samFile;
};

RdpConnection::RdpConnection(Server *server, qintptr socketHandle)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->server = server;
    d->socketHandle = socketHandle;

    d->inputHandler = std::make_unique<InputHandler>(this);
    d->videoStream = std::make_unique<VideoStream>(this);
    connect(d->videoStream.get(), &VideoStream::closed, this, [this]() {
        if (d->state == State::Running || d->state == State::Streaming) {
            qCDebug(KRDP) << "Video stream closed, closing session";
            d->peer->Close(d->peer);
        }
    });
    d->cursor = std::make_unique<Cursor>(this);
    d->networkDetection = std::make_unique<NetworkDetection>(this);

    QMetaObject::invokeMethod(this, &RdpConnection::initialize, Qt::QueuedConnection);
}

RdpConnection::~RdpConnection()
{
    if (d->state == State::Streaming) {
        d->peer->Close(d->peer);
    }

    if (d->thread.joinable()) {
        d->thread.request_stop();
        d->thread.join();
    }

    if (d->peer) {
        freerdp_peer_free(d->peer);
    }
}

RdpConnection::State RdpConnection::state() const
{
    return d->state;
}

void RdpConnection::setState(KRdp::RdpConnection::State newState)
{
    if (newState == d->state) {
        return;
    }

    d->state = newState;
    Q_EMIT stateChanged(newState);
}

void RdpConnection::close(RdpConnection::CloseReason reason)
{
    switch (reason) {
    case CloseReason::VideoInitFailed:
        freerdp_set_error_info(d->peer->context->rdp, ERRINFO_GRAPHICS_SUBSYSTEM_FAILED);
        break;
    case CloseReason::None:
        break;
    }

    d->peer->Close(d->peer);
}

InputHandler *RdpConnection::inputHandler() const
{
    return d->inputHandler.get();
}

KRdp::VideoStream *RdpConnection::videoStream() const
{
    return d->videoStream.get();
}

Cursor *RdpConnection::cursor() const
{
    return d->cursor.get();
}

NetworkDetection *RdpConnection::networkDetection() const
{
    return d->networkDetection.get();
}

void RdpConnection::initialize()
{
    setState(State::Starting);

    d->peer = freerdp_peer_new(d->socketHandle);
    if (!d->peer) {
        qCWarning(KRDP) << "Failed to create peer";
        return;
    }

    // Create an instance of our custom PeerContext extended context as context
    // rather than the plain rdpContext.
    d->peer->ContextSize = sizeof(PeerContext);
    d->peer->ContextNew = (psPeerContextNew)newPeerContext;
    d->peer->ContextFree = (psPeerContextFree)freePeerContext;

#ifdef FREERDP3
    auto result = freerdp_peer_context_new_ex(d->peer, d->server->rdpSettings());
#else
    auto result = freerdp_peer_context_new(d->peer);
#endif
    if (!result) {
        qCWarning(KRDP) << "Failed to create peer context";
        return;
    }

    auto context = reinterpret_cast<PeerContext *>(d->peer->context);
    context->connection = this;

    auto settings = d->peer->context->settings;

    createSamFile(d->samFile, d->server->users());
    if (!freerdp_settings_set_string(settings, FreeRDP_NtlmSamFile, d->samFile.fileName().toUtf8().data())) {
        qCWarning(KRDP) << "Failed to set SAM database";
        return;
    }

#ifdef FREERDP3
    auto certificate = freerdp_certificate_new_from_file(d->server->tlsCertificate().toUtf8().data());
    if (!certificate) {
        qCWarning(KRDP) << "Could not read certificate file" << d->server->tlsCertificate();
        return;
    }
    freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, certificate, 1);

    auto key = freerdp_key_new_from_file(d->server->tlsCertificateKey().toUtf8().data());
    if (!key) {
        qCWarning(KRDP) << "Could not read certificate file" << d->server->tlsCertificate();
        return;
    }
    freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key, 1);
#else
    settings->CertificateFile = strdup(d->server->tlsCertificate().string().data());
    settings->PrivateKeyFile = strdup(d->server->tlsCertificateKey().string().data());
#endif

    // Only NTLM Authentication (NLA) security is currently supported. This also
    // happens to be the most secure one. It implicitly requires a TLS
    // connection so the above certificate is always required.
    settings->RdpSecurity = false;
    settings->TlsSecurity = false;
    settings->NlaSecurity = true;

    settings->OsMajorType = OSMAJORTYPE_UNIX;
    // PSEUDO_XSERVER is apparently required for things to work properly.
    settings->OsMinorType = OSMINORTYPE_PSEUDO_XSERVER;

    // TODO: Implement audio support
    settings->AudioPlayback = false;

    settings->ColorDepth = 32;

    // Plain YUV420 AVC is currently the most straightforward of the the AVC
    // related codecs to implement. Moreover, it makes the encoding side also
    // simpler so it is currently the only supported codec. This uses the RdpGfx
    // pipeline, so make sure to request that.
    settings->SupportGraphicsPipeline = true;
    settings->GfxAVC444 = false;
    settings->GfxAVC444v2 = false;
    settings->GfxH264 = true;

    settings->GfxSmallCache = false;
    settings->GfxThinClient = false;

    settings->HasExtendedMouseEvent = true;
    settings->HasHorizontalWheel = true;
    settings->UnicodeInput = true;

    // TODO: Implement network performance detection
    settings->NetworkAutoDetect = true;

    settings->RefreshRect = true;
    settings->RemoteConsoleAudio = true;
    settings->RemoteFxCodec = false;
    settings->NSCodec = false;
    settings->FrameMarkerCommandEnabled = true;
    settings->SurfaceFrameMarkerEnabled = true;

    d->peer->Capabilities = peerCapabilities;
    d->peer->Activate = peerActivate;
    d->peer->PostConnect = peerPostConnect;

    d->peer->update->SuppressOutput = suppressOutput;

    d->inputHandler->initialize(d->peer->context->input);
    context->inputHandler = d->inputHandler.get();

    context->networkDetection = d->networkDetection.get();
    d->networkDetection->initialize();

    if (!d->peer->Initialize(d->peer)) {
        qCWarning(KRDP) << "Unable to initialize peer";
        return;
    }

    qCDebug(KRDP) << "Session setup completed, start processing...";

    // Perform actual communication on a separate thread.
    d->thread = std::jthread(std::bind(&RdpConnection::run, this, std::placeholders::_1));
    pthread_setname_np(d->thread.native_handle(), "krdp_session");
}

void RdpConnection::run(std::stop_token stopToken)
{
    auto context = reinterpret_cast<PeerContext *>(d->peer->context);
    auto channelEvent = WTSVirtualChannelManagerGetEventHandle(context->virtualChannelManager);

    setState(State::Running);

    while (!stopToken.stop_requested()) {
        std::array<HANDLE, 32> events{channelEvent};
        auto handleCount = d->peer->GetEventHandles(d->peer, events.data() + 1, 31);
        if (handleCount <= 0) {
            qCDebug(KRDP) << "Unable to get transport event handles";
            break;
        }
        // Wait for something to happen on the connection.
        WaitForMultipleObjects(1 + handleCount, events.data(), FALSE, INFINITE);

        // Read data from the socket and have FreeRDP process it.
        if (d->peer->CheckFileDescriptor(d->peer) != TRUE) {
            qCDebug(KRDP) << "Unable to check file descriptor";
            break;
        }

        // Read data for the virtual channel manager.
        // Note that this is separate from the above file handle for... some reason.
        // However, if we don't call this, login and any dynamic channels will not work.
        if (WTSVirtualChannelManagerCheckFileDescriptor(context->virtualChannelManager) != TRUE) {
            qCDebug(KRDP) << "Unable to check Virtual Channel Manager file descriptor, closing connection";
            break;
        }

        // Initialize any dynamic channels once the dynamic channel channel is setup.
        if (WTSVirtualChannelManagerIsChannelJoined(context->virtualChannelManager, DRDYNVC_SVC_CHANNEL_NAME)) {
            // Dynamic channels can only be set up properly once the dynamic channel channel is properly setup.
            if (WTSVirtualChannelManagerGetDrdynvcState(context->virtualChannelManager) == DRDYNVC_STATE_READY) {
                if (d->videoStream->initialize()) {
                    d->videoStream->setEnabled(true);
                    setState(State::Streaming);
                } else {
                    break;
                }
            }
        }

        d->networkDetection->update();
    }

    qCDebug(KRDP) << "Closing session";
    onClose();
}

bool RdpConnection::onCapabilities()
{
    auto settings = d->peer->context->settings;
    // We only support GraphicsPipeline clients currently as that is required
    // for AVC streaming.
    if (!settings->SupportGraphicsPipeline) {
        qCWarning(KRDP) << "Client does not support graphics pipeline which is required";
        return false;
    }

    if (settings->ColorDepth != 32) {
        qCDebug(KRDP) << "Correcting invalid color depth from client:" << settings->ColorDepth;
        settings->ColorDepth = 32;
    }

    if (!settings->DesktopResize) {
        qCWarning(KRDP) << "Client doesn't support resizing, aborting";
        return false;
    }

    if (settings->PointerCacheSize <= 0) {
        qCWarning(KRDP) << "Client doesn't support pointer caching, aborting";
        return false;
    }

    return true;
}

bool RdpConnection::onActivate()
{
    return true;
}

bool RdpConnection::onPostConnect()
{
    qCInfo(KRDP) << "New client connected:" << d->peer->hostname << freerdp_peer_os_major_type_string(d->peer) << freerdp_peer_os_minor_type_string(d->peer);

    // Cleanup the temporary file so we don't leak it.
    d->samFile.remove();

    return true;
}

bool RdpConnection::onClose()
{
    d->videoStream->close();
    setState(State::Closed);
    return true;
}

bool RdpConnection::onSuppressOutput(uint8_t allow)
{
    if (allow) {
        d->videoStream->setEnabled(true);
    } else {
        d->videoStream->setEnabled(false);
    }

    return true;
}

freerdp_peer *RdpConnection::rdpPeer() const
{
    return d->peer;
}

rdpContext *RdpConnection::rdpPeerContext() const
{
    return d->peer->context;
}
}

#include "moc_RdpConnection.cpp"
