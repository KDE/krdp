// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "DaemonPeerContext_p.h"

#include <freerdp/peer.h>

#include "krdpd_logging.h"

namespace KRdp
{

DaemonPeerContext *contextForPeer(freerdp_peer *peer)
{
    return reinterpret_cast<DaemonPeerContext *>(peer->context);
}

}

BOOL newPeerContext(freerdp_peer *peer, rdpContext *context)
{
    auto peerContext = reinterpret_cast<KRdp::DaemonPeerContext *>(context);

    // Initialize the virtual channel manager, so that we can create new
    // dynamic channels.
    peerContext->virtualChannelManager = WTSOpenServerA((LPSTR)peer->context);
    if (!peerContext->virtualChannelManager || peerContext->virtualChannelManager == INVALID_HANDLE_VALUE) {
        qCWarning(KRDPD) << "Failed creating virtual channel manager";
        freerdp_peer_context_free(peer);
        return FALSE;
    }

    return TRUE;
}

void freePeerContext(freerdp_peer * /*peer*/, rdpContext *context)
{
    auto peerContext = reinterpret_cast<KRdp::DaemonPeerContext *>(context);

    if (!peerContext) {
        return;
    }

    WTSCloseServer(peerContext->virtualChannelManager);
    peerContext->virtualChannelManager = nullptr;
}
