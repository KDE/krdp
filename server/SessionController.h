// SPDX-FileCopyrightText: 2024 Arjen Hiemstra <ahiemstra@heimr.nl>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "RdpConnection.h"
#include <KStatusNotifierItem>
#include <vector>

#include <QObject>

namespace KRdp
{
class AbstractSession;
class Server;
class RdpConnection;
}

class SessionWrapper;

class SessionController : public QObject
{
    Q_OBJECT
public:
    SessionController(KRdp::Server *server);
    ~SessionController() override;

    void setUsePlasmaSession(bool plasma);
    void setMonitorIndex(const std::optional<int> &index);
    void setQuality(const std::optional<int> &quality);
    void setSNIStatus(const KRdp::RdpConnection::State state);
    void stopFromSNI();

private:
    void onNewConnection(KRdp::RdpConnection *newConnection);
    void removeConnection(KRdp::RdpConnection *connection);

    KRdp::Server *m_server = nullptr;
    bool m_usePlasmaSession = false;
    std::optional<int> m_monitorIndex;
    std::optional<int> m_quality;

    std::vector<std::unique_ptr<SessionWrapper>> m_wrappers;

    KStatusNotifierItem *m_sni;
};
