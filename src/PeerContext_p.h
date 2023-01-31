// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <freerdp/freerdp.h>

namespace KRdp
{

class Session;
class InputHandler;

struct PeerContext {
    rdpContext _p;

    Session *session = nullptr;
    InputHandler *inputHandler = nullptr;

    HANDLE virtualChannelManager = nullptr;
};

}

BOOL newPeerContext(freerdp_peer *peer, rdpContext *peer_context);
void freePeerContext(freerdp_peer *peer, rdpContext *peer_context);
