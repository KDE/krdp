// SPDX-FileCopyrightText: 2024 Arjen Hiemstra <ahiemstra@heimr.nl>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "RdpConnection.h"
#include "krdpserversettings.h"
#include <AbstractSession.h>
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
    Q_ENUM(SessionType)

    SessionController(KRdp::Server *server, SessionType sessionType);
    ~SessionController() override;

    void setVirtualMonitor(const KRdp::VirtualMonitor &vm);
    void setMonitorIndex(const std::optional<int> &index);
    void setQuality(const std::optional<int> &quality);
    ServerConfig::OperationMode operationMode() const;
    void setOperationMode(ServerConfig::OperationMode mode);
    void setSNIStatus(const KRdp::RdpConnection::State state);
    void stopFromSNI();

private:
    void onNewConnection(KRdp::RdpConnection *newConnection);
    std::unique_ptr<KRdp::AbstractSession> makeSession();
    // Lock/unlock the desktop session via logind when operating in remote-access mode.
    void setSessionLocked(bool locked);
    void switchToGreeter();

    KRdp::Server *m_server = nullptr;
    SessionType m_sessionType;
    std::optional<int> m_monitorIndex;
    std::optional<int> m_quality;
    std::optional<KRdp::VirtualMonitor> m_virtualMonitor;

    std::unique_ptr<KRdp::AbstractSession> m_initializationSession;

    std::vector<std::unique_ptr<SessionWrapper>> m_wrappers;

    ServerConfig::OperationMode m_operatingMode = ServerConfig::SharedAccess;

    KStatusNotifierItem *m_sni;
};
