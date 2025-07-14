// SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "DisplayControl.h"
#include "PeerContext_p.h"
#include "RdpConnection.h"

#include <QDebug>
#include <QSize>

#include <freerdp/peer.h>

#include "krdp_logging.h"

using namespace KRdp;

static UINT display_control_receive_monitor_layout(DispServerContext *context, const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU *pdu)
{
    if (pdu->NumMonitors != 1) {
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

bool DisplayControl::initialize()
{
    if (m_dispManager) {
        return true;
    }

    auto peerContext = reinterpret_cast<PeerContext *>(m_session->rdpPeer()->context);

    m_dispManager = disp_server_context_new(peerContext->virtualChannelManager);
    if (!m_dispManager) {
        qCWarning(KRDP) << "Failed creating DisplayControl context";
        return false;
    }

    m_dispManager->rdpcontext = m_session->rdpPeer()->context;
    m_dispManager->custom = this;

    m_dispManager->DispMonitorLayout = display_control_receive_monitor_layout;

    m_dispManager->MaxNumMonitors = 1;
    m_dispManager->MaxMonitorAreaFactorA = 8192;
    m_dispManager->MaxMonitorAreaFactorB = 8192;

    if (m_dispManager->Open(m_dispManager) != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "Could not open DisplayControl context";
        return false;
    }
    if (m_dispManager->DisplayControlCaps(m_dispManager) != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "Could not advertise DisplayControl capabilities";
        return false;
    }

    return true;
}

void DisplayControl::close()
{
    if (m_dispManager) {
        disp_server_context_free(m_dispManager);
        m_dispManager = nullptr;
    }
}
