// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PeerContext_p.h"

#include <freerdp/peer.h>

#include "krdp_logging.h"

namespace KRdp
{

PeerContext *contextForPeer(freerdp_peer *peer)
{
    return reinterpret_cast<PeerContext *>(peer->context);
}

}

BOOL newPeerContext(freerdp_peer *peer, rdpContext *context)
{
    auto peerContext = reinterpret_cast<KRdp::PeerContext *>(context);

    // Initialize the virtual channel manager, so that we can create new
    // dynamic channels.
    peerContext->virtualChannelManager = WTSOpenServerA((LPSTR)peer->context);
    if (!peerContext->virtualChannelManager || peerContext->virtualChannelManager == INVALID_HANDLE_VALUE) {
        qCWarning(KRDP) << "Failed creating virtual channel manager";
        freerdp_peer_context_free(peer);
        return FALSE;
    }

    return TRUE;
}

void freePeerContext(freerdp_peer *peer, rdpContext *context)
{
    auto peerContext = reinterpret_cast<KRdp::PeerContext *>(context);

    if (!peerContext) {
        return;
    }

    WTSCloseServer(peerContext->virtualChannelManager);
    peerContext->virtualChannelManager = nullptr;
}
