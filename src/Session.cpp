// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Session.h"

#include <QTcpSocket>
#include <QThread>

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>

#include "krdp_logging.h"

using namespace KRdp;

// void threadFunction(Session *session);

struct PeerContext {
    rdpContext *context;
};

static BOOL newPeerContext(freerdp_peer *peer, PeerContext *peer_context)
{
    // TODO
    return TRUE;
}

static void freePeerContext(freerdp_peer *peer, PeerContext *peer_context)
{
    if (!peer_context) {
        return;
    }

    // TODO
}

class KRDP_NO_EXPORT Session::Private
{
public:
    // std::unique_ptr<QThread> thread;
    std::jthread thread;
    std::unique_ptr<QTcpSocket> socket;
    freerdp_peer *peer = nullptr;
};

Session::Session(qintptr socketHandle)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->socket = std::make_unique<QTcpSocket>();
    if (!d->socket->setSocketDescriptor(socketHandle)) {
        // error
    }

    d->thread = std::jthread(&Session::run, this);
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

void Session::run(std::stop_token stopToken)
{
    d->peer = freerdp_peer_new(d->socket->socketDescriptor());
    if (!d->peer) {
        qCWarning(KRDP) << "Failed to create peer";
        return;
    }

    d->peer->ContextSize = sizeof(PeerContext);
    d->peer->ContextNew = (psPeerContextNew)newPeerContext;
    d->peer->ContextFree = (psPeerContextFree)freePeerContext;

    if (!freerdp_peer_context_new(d->peer)) {
        qCWarning(KRDP) << "Failed to create peer context";
    }

    auto settings = d->peer->settings;
    settings->RdpSecurity = false;
    settings->TlsSecurity = false;
    settings->NlaSecurity = false;

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
    settings->NetworkAutoDetect = true;
    settings->RefreshRect = true;
    settings->RemoteConsoleAudio = true;
    settings->RemoteFxCodec = true;
    settings->SupportGraphicsPipeline = true;
    settings->NSCodec = true;
    settings->FrameMarkerCommandEnabled = true;
    settings->SurfaceFrameMarkerEnabled = true;
    settings->UnicodeInput = true;

    if (!d->peer->Initialize(d->peer)) {
        qCWarning(KRDP) << "Unable to initialize peer";
    }

    qCDebug(KRDP) << "Session setup completed, start processing...";

    while (!stopToken.stop_requested()) {
        // do stuff
    }

    qCDebug(KRDP) << "Closing session";
}
