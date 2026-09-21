// SPDX-FileCopyrightText: 2026 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "EiInputHandler.h"

#include <QDebug>

#include <freerdp/server/rdpei.h>

#include "PeerContext_p.h"
#include "RdpConnection.h"

using namespace KRdp;

namespace
{

UINT onClientReady(RdpeiServerContext *context)
{
    qDebug() << "RDPEI client ready:" << context->clientVersion << "with" << context->maxTouchPoints << "touch points";
    return CHANNEL_RC_OK;
}

UINT onTouchEvent(RdpeiServerContext *, const RDPINPUT_TOUCH_EVENT *event)
{
    qDebug() << "RDPEI touch event with" << event->frameCount << "frames";
    return CHANNEL_RC_OK;
}

UINT onPenEvent(RdpeiServerContext *, const RDPINPUT_PEN_EVENT *event)
{
    qDebug() << "RDPEI pen event with" << event->frameCount << "frames";
    return CHANNEL_RC_OK;
}

UINT onTouchReleased(RdpeiServerContext *, BYTE contactId)
{
    qDebug() << "RDPEI touch released:" << contactId;
    return CHANNEL_RC_OK;
}

}

EiInputHandler::EiInputHandler(RdpConnection *session)
    : m_session(session)
{
}

EiInputHandler::~EiInputHandler()
{
    close();
}

bool EiInputHandler::initialize()
{
    if (m_context) {
        return true;
    }

    auto peerContext = reinterpret_cast<PeerContext *>(m_session->rdpPeer()->context);
    m_context = rdpei_server_context_new(peerContext->virtualChannelManager);
    if (!m_context) {
        qWarning() << "Failed creating RDPEI context";
        return false;
    }

    m_context->onClientReady = onClientReady;
    m_context->onTouchEvent = onTouchEvent;
    m_context->onPenEvent = onPenEvent;
    m_context->onTouchReleased = onTouchReleased;

    if (m_context->Open(m_context) != CHANNEL_RC_OK) {
        qWarning() << "Could not open RDPEI context";
        rdpei_server_context_free(m_context);
        m_context = nullptr;
        return false;
    }

    return true;
}

void EiInputHandler::close()
{
    if (!m_context) {
        return;
    }

    m_context->Close(m_context);
    rdpei_server_context_free(m_context);
    m_context = nullptr;
}
