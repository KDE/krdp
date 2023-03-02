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

BOOL peerCapabilities(freerdp_peer *peer)
{
    auto context = reinterpret_cast<PeerContext *>(peer->context);
    if (context->session->onCapabilities()) {
        return TRUE;
    }

    return FALSE;
}

BOOL peerPostConnect(freerdp_peer *peer)
{
    auto context = reinterpret_cast<PeerContext *>(peer->context);
    if (context->session->onPostConnect()) {
        return TRUE;
    }

    return FALSE;
}

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
    settings->CertificateFile = strdup(d->server->tlsCertificate().toUtf8().data());
    settings->PrivateKeyFile = strdup(d->server->tlsCertificateKey().toUtf8().data());
#endif

    settings->RdpSecurity = false;
    settings->TlsSecurity = true;
    settings->NlaSecurity = true;

    settings->OsMajorType = OSMAJORTYPE_UNIX;
    settings->OsMinorType = OSMINORTYPE_PSEUDO_XSERVER;

    settings->AudioPlayback = false;

    settings->ColorDepth = 32;
    settings->GfxAVC444v2 = false;
    settings->GfxH264 = true;
    settings->GfxSmallCache = false;
    settings->GfxThinClient = false;

    settings->HasExtendedMouseEvent = true;
    settings->HasHorizontalWheel = true;
    settings->NetworkAutoDetect = false;
    settings->RefreshRect = true;
    settings->RemoteConsoleAudio = true;
    settings->RemoteFxCodec = true;
    settings->SupportGraphicsPipeline = true;
    settings->NSCodec = false;
    settings->FrameMarkerCommandEnabled = true;
    settings->SurfaceFrameMarkerEnabled = true;
    settings->UnicodeInput = true;

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
        WaitForMultipleObjects(1 + handleCount, events.data(), FALSE, INFINITE);

        if (d->peer->CheckFileDescriptor(d->peer) != TRUE) {
            qCDebug(KRDP) << "Unable to check file descriptor";
            onClose();
            break;
        }

        if (WTSVirtualChannelManagerCheckFileDescriptor(context->virtualChannelManager) != TRUE) {
            qCDebug(KRDP) << "Unable to check Virtual Channel Manager file descriptor, closing connection";
            onClose();
            break;
        }

        if (WTSVirtualChannelManagerIsChannelJoined(context->virtualChannelManager, DRDYNVC_SVC_CHANNEL_NAME)) {
            switch (WTSVirtualChannelManagerGetDrdynvcState(context->virtualChannelManager)) {
            case DRDYNVC_STATE_NONE:
            case DRDYNVC_STATE_INITIALIZED:
                break;
            case DRDYNVC_STATE_READY:
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
    if (!settings->SupportGraphicsPipeline) {
        qCWarning(KRDP) << "Client does not support graphics pipeline which is required";
        return false;
    }

    return true;
}

bool Session::onActivate()
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__;

    return true;
}

bool Session::onPostConnect()
{
    qCInfo(KRDP) << "New client connected:" << d->peer->hostname << freerdp_peer_os_major_type_string(d->peer) << freerdp_peer_os_minor_type_string(d->peer);

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
