// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <freerdp/freerdp.h>

namespace KRdp
{

class RdpConnection;
class InputHandler;
class VideoStream;
class NetworkDetection;

/**
 * Extension of the FreeRDP Peer Context used to store extra data for KRdp.
 */
struct PeerContext {
    // The base rdpContext structure.
    // Important: This should remain as a plain value as that is how the
    // extension mechanism works.
    rdpContext _p;

    RdpConnection *connection = nullptr;
    InputHandler *inputHandler = nullptr;
    VideoStream *stream = nullptr;
    NetworkDetection *networkDetection = nullptr;

    HANDLE virtualChannelManager = nullptr;
};

// Convenience method to get the PeerContext instance for a specific FreeRDP peer.
PeerContext *contextForPeer(freerdp_peer *peer);
}

// FreeRDP callbacks used to initialize a PeerContext during creation/destruction.
BOOL newPeerContext(freerdp_peer *peer, rdpContext *peer_context);
void freePeerContext(freerdp_peer *peer, rdpContext *peer_context);
