// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Session.h"

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

#include "InputHandler.h"
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
bool createSamFile(QTemporaryFile &file, const QString &username, const QString &password)
{
    auto runtimePath = fs::path(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation).toStdString());

    auto path = runtimePath / "krdp";
    fs::create_directories(path);

    file.setFileTemplate(QString::fromStdString(path / "rdp-sam-XXXXXX"));
    if (!file.open()) {
        qCWarning(KRDP) << "Could not open SAM file";
        return false;
    }

    auto pw = password.toUtf8();
    std::array<uint8_t, 16> hash;
    NTOWFv1A((LPSTR)pw.data(), pw.size(), hash.data());

    auto data = QStringLiteral("%1:::").arg(username);
    for (int i = 0; i < 16; ++i) {
        data.append(QStringLiteral("%1").arg(hash[i], 2, 16, QLatin1Char('0')));
    }
    data.append(QStringLiteral(":::\n"));

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
    if (context->session->onCapabilities()) {
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
    if (context->session->onPostConnect()) {
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
    if (context->session->onActivate()) {
        return TRUE;
    }

    return FALSE;
}

class KRDP_NO_EXPORT Session::Private
{
public:
    Server *server = nullptr;

    State state = State::Initial;

    qintptr socketHandle;

    std::unique_ptr<InputHandler> inputHandler;
    std::unique_ptr<VideoStream> videoStream;

    freerdp_peer *peer = nullptr;

    std::jthread thread;

    QTemporaryFile samFile;
};

Session::Session(Server *server, qintptr socketHandle)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->server = server;
    d->socketHandle = socketHandle;

    d->inputHandler = std::make_unique<InputHandler>(this);
    d->videoStream = std::make_unique<VideoStream>(this);

    QMetaObject::invokeMethod(this, &Session::initialize, Qt::QueuedConnection);
}

Session::~Session()
{
    if (d->thread.joinable()) {
        d->thread.request_stop();
        d->thread.join();
    }

    if (d->peer) {
        freerdp_peer_free(d->peer);
    }
}

Session::State Session::state() const
{
    return d->state;
}

void Session::setState(KRdp::Session::State newState)
{
    if (newState == d->state) {
        return;
    }

    d->state = newState;
    Q_EMIT stateChanged();
}

InputHandler *Session::inputHandler() const
{
    return d->inputHandler.get();
}

KRdp::VideoStream *Session::videoStream() const
{
    return d->videoStream.get();
}

void Session::initialize()
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
    context->session = this;

    auto settings = d->peer->context->settings;

    createSamFile(d->samFile, d->server->userName(), d->server->password());
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
    settings->NetworkAutoDetect = false;

    settings->RefreshRect = true;
    settings->RemoteConsoleAudio = true;
    settings->RemoteFxCodec = false;
    settings->NSCodec = false;
    settings->FrameMarkerCommandEnabled = true;
    settings->SurfaceFrameMarkerEnabled = true;

    d->peer->Capabilities = peerCapabilities;
    d->peer->Activate = peerActivate;
    d->peer->PostConnect = peerPostConnect;

    d->inputHandler->initialize(d->peer->context->input);
    context->inputHandler = d->inputHandler.get();

    if (!d->peer->Initialize(d->peer)) {
        qCWarning(KRDP) << "Unable to initialize peer";
        return;
    }

    qCDebug(KRDP) << "Session setup completed, start processing...";

    // Perform actual communication on a separate thread.
    d->thread = std::jthread(&Session::run, this);
}

void Session::run(std::stop_token stopToken)
{
    auto context = reinterpret_cast<PeerContext *>(d->peer->context);
    auto channelEvent = WTSVirtualChannelManagerGetEventHandle(context->virtualChannelManager);

    setState(State::Running);

    while (!stopToken.stop_requested()) {
        std::array<HANDLE, 32> events{channelEvent};
        auto handleCount = d->peer->GetEventHandles(d->peer, events.data() + 1, 31);
        if (handleCount <= 0) {
            qCDebug(KRDP) << "Unable to get transport event handles";
            onClose();
            break;
        }
        // Wait for something to happen on the connection.
        WaitForMultipleObjects(1 + handleCount, events.data(), FALSE, INFINITE);

        // Read data from the socket and have FreeRDP process it.
        if (d->peer->CheckFileDescriptor(d->peer) != TRUE) {
            qCDebug(KRDP) << "Unable to check file descriptor";
            onClose();
            break;
        }

        // Read data for the virtual channel manager.
        // Note that this is separate from the above file handle for... some reason.
        // However, if we don't call this, login and any dynamic channels will not work.
        if (WTSVirtualChannelManagerCheckFileDescriptor(context->virtualChannelManager) != TRUE) {
            qCDebug(KRDP) << "Unable to check Virtual Channel Manager file descriptor, closing connection";
            onClose();
            break;
        }

        // Initialize any dynamic channels once the dynamic channel channel is setup.
        if (WTSVirtualChannelManagerIsChannelJoined(context->virtualChannelManager, DRDYNVC_SVC_CHANNEL_NAME)) {
            // Dynamic channels can only be set up properly once the dynamic channel channel is properly setup.
            if (WTSVirtualChannelManagerGetDrdynvcState(context->virtualChannelManager) == DRDYNVC_STATE_READY) {
                d->videoStream->initialize();
                break;
            }
        }
    }

    qCDebug(KRDP) << "Closing session";
    onClose();
}

bool Session::onCapabilities()
{
    auto settings = d->peer->context->settings;
    // We only support GraphicsPipeline clients currently as that is required
    // for AVC streaming.
    if (!settings->SupportGraphicsPipeline) {
        qCWarning(KRDP) << "Client does not support graphics pipeline which is required";
        return false;
    }

    return true;
}

bool Session::onActivate()
{
    return true;
}

bool Session::onPostConnect()
{
    qCInfo(KRDP) << "New client connected:" << d->peer->hostname << freerdp_peer_os_major_type_string(d->peer) << freerdp_peer_os_minor_type_string(d->peer);

    // Cleanup the temporary file so we don't leak it.
    d->samFile.remove();

    return true;
}

bool Session::onClose()
{
    d->videoStream->close();
    setState(State::Closed);
    return true;
}

freerdp_peer *Session::rdpPeer() const
{
    return d->peer;
}
}
