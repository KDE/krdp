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
#include <freerdp/peer.h>
#include <freerdp/settings.h>

#include "InputHandler.h"
#include "PeerContext_p.h"
#include "Server.h"

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

void Session::initialize()
{
    d->peer = freerdp_peer_new(d->socketHandle);
    if (!d->peer) {
        qCWarning(KRDP) << "Failed to create peer";
        return;
    }

    d->peer->ContextSize = sizeof(PeerContext);
    d->peer->ContextNew = (psPeerContextNew)newPeerContext;
    d->peer->ContextFree = (psPeerContextFree)freePeerContext;

    if (!freerdp_peer_context_new_ex(d->peer, d->server->rdpSettings())) {
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

    settings->CertificateFile = strdup(d->server->tlsCertificate().toUtf8().data());
    settings->PrivateKeyFile = strdup(d->server->tlsCertificateKey().toUtf8().data());

    settings->RdpSecurity = false;
    settings->TlsSecurity = true;
    settings->NlaSecurity = true;

    settings->OsMajorType = OSMAJORTYPE_UNIX;
    settings->OsMinorType = OSMINORTYPE_PSEUDO_XSERVER;

    settings->AudioPlayback = false;

    settings->ColorDepth = 32;
    settings->GfxAVC444v2 = true;
    settings->GfxH264 = false;
    settings->GfxSmallCache = false;
    settings->GfxThinClient = false;

    settings->HasExtendedMouseEvent = true;
    settings->HasHorizontalWheel = true;
    settings->NetworkAutoDetect = false;
    settings->RefreshRect = true;
    settings->RemoteConsoleAudio = true;
    settings->RemoteFxCodec = true;
    settings->SupportGraphicsPipeline = true;
    settings->NSCodec = true;
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

        if (!d->peer->CheckFileDescriptor(d->peer)) {
            qCDebug(KRDP) << "Unable to check file descriptor";
            onClose();
            break;
        }

        if (WTSVirtualChannelManagerIsChannelJoined(context->virtualChannelManager, "drdynvc")) {
            switch (WTSVirtualChannelManagerGetDrdynvcState(context->virtualChannelManager)) {
            case DRDYNVC_STATE_NONE:
            case DRDYNVC_STATE_INITIALIZED:
                break;
            case DRDYNVC_STATE_READY:
                // TODO initialize
                break;
            }
        }

        if (!WTSVirtualChannelManagerCheckFileDescriptor(context->virtualChannelManager)) {
            qCDebug(KRDP) << "Unable to check Virtual Channel Manager file descriptor, closing connection";
            onClose();
            break;
        }
    }

    qCDebug(KRDP) << "Closing session";
    setState(State::Closed);
}

bool Session::onCapabilities()
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__;
    return true;
}

bool Session::onActivate()
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__;
    return true;
}

bool Session::onPostConnect()
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__;
    return true;
}

bool Session::onClose()
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__;
    return true;
}

}
