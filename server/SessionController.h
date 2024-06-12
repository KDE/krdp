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
    enum class SessionType {
        Portal,
        Plasma,
    };

    SessionController(KRdp::Server *server, SessionType sessionType);
    ~SessionController() override;

    void setMonitorIndex(const std::optional<int> &index);
    void setQuality(const std::optional<int> &quality);
    void setSNIStatus(const KRdp::RdpConnection::State state);
    void stopFromSNI();

private:
    void onNewConnection(KRdp::RdpConnection *newConnection);
    void removeConnection(KRdp::RdpConnection *connection);
    std::unique_ptr<KRdp::AbstractSession> makeSession();

    KRdp::Server *m_server = nullptr;
    SessionType m_sessionType;
    std::optional<int> m_monitorIndex;
    std::optional<int> m_quality;

    std::unique_ptr<KRdp::AbstractSession> m_initializationSession;

    std::vector<std::unique_ptr<SessionWrapper>> m_wrappers;

    KStatusNotifierItem *m_sni;
};
