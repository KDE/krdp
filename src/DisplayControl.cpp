// SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "DisplayControl.h"
#include "PeerContext_p.h"
#include "RdpConnection.h"

#include <QDebug>
#include <QSize>

using namespace KRdp;

static BOOL disp_channel_id_assigned(DispServerContext *disp_context, uint32_t channel_id)
{
    qDebug() << "bound";
    Q_UNUSED(disp_context)
    return TRUE;
}

static UINT display_control_receive_monitor_layout(DispServerContext *context, const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU *pdu)
{
    if (pdu->NumMonitors < 0) {
        return CHANNEL_RC_BAD_CHANNEL;
    }
    if (pdu->NumMonitors > 1) {
        return CHANNEL_RC_BAD_CHANNEL;
    }
    QSize monitorSize = QSize(pdu->Monitors[0].Width, pdu->Monitors[0].Height);

    auto c = static_cast<DisplayControl *>(context->custom);
    Q_EMIT c->requestedScreenSizeChanged(monitorSize);

    return CHANNEL_RC_OK;
}

DisplayControl::DisplayControl(RdpConnection *session)
    : m_session(session)
{
}

DisplayControl::~DisplayControl()
{
    if (m_dispManager) {
        disp_server_context_free(m_dispManager);
        m_dispManager = nullptr;
    }
}

bool DisplayControl::initialize()
{
    auto peerContext = reinterpret_cast<PeerContext *>(m_session->rdpPeerContext());

    m_dispManager = disp_server_context_new(peerContext->virtualChannelManager);
    m_dispManager->rdpcontext = m_session->rdpPeerContext();
    m_dispManager->custom = this;
    m_dispManager->DispMonitorLayout = display_control_receive_monitor_layout;
    // m_dispManager->ChannelIdAssigned = disp_channel_id_assigned;

    m_dispManager->MaxNumMonitors = 1;
    m_dispManager->MaxMonitorAreaFactorA = 8192;
    m_dispManager->MaxMonitorAreaFactorB = 8192;

    m_dispManager->Open(m_dispManager);
    m_dispManager->DisplayControlCaps(m_dispManager);

    return true;
}
