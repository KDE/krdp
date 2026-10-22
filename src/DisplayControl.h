// SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "freerdp/server/disp.h"
#include <QList>
#include <QObject>

#include "AbstractSession.h"

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

class KRDP_EXPORT DisplayControl : public QObject
{
    Q_OBJECT
public:
    explicit DisplayControl(RdpConnection *session);
    ~DisplayControl() = default;

    bool initialize();
    void close();

    QList<VirtualMonitor> display() const;

Q_SIGNALS:
    void displayLayoutChanged();

private:
    static UINT displayControlReceiveMonitorLayout(DispServerContext *context, const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU *pdu);
    UINT receiveMonitorLayout(const DISPLAY_CONTROL_MONITOR_LAYOUT_PDU *pdu);

    RdpConnection *m_session = nullptr;
    DispServerContext *m_dispManager = nullptr;
    QList<VirtualMonitor> m_display;
};

}
