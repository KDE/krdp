// SPDX-FileCopyrightText: 2024 Arjen Hiemstra <ahiemstra@heimr.nl>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "RdpConnection.h"
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

    SessionController(KRdp::Server *server, SessionType sessionType);
    ~SessionController() override;

    void setVirtualMonitor(const KRdp::VirtualMonitor &vm);
    void setMonitorIndex(const std::optional<int> &index);
    void setQuality(const std::optional<int> &quality);
    void setSNIStatus(const KRdp::RdpConnection::State state);
    void stopFromSNI();

    /**
     * When enabled, lock the desktop session as the last client disconnects and
     * unlock it (via logind) when a client connects, so the physical machine is
     * left locked while no one is using it remotely.
     */
    void setLockOnDisconnect(bool lock);

private:
    void onNewConnection(KRdp::RdpConnection *newConnection);
    std::unique_ptr<KRdp::AbstractSession> makeSession();
    // Lock/unlock the desktop session via logind (no-op unless setLockOnDisconnect(true)).
    void setSessionLocked(bool locked);

    KRdp::Server *m_server = nullptr;
    SessionType m_sessionType;
    std::optional<int> m_monitorIndex;
    std::optional<int> m_quality;
    std::optional<KRdp::VirtualMonitor> m_virtualMonitor;

    std::unique_ptr<KRdp::AbstractSession> m_initializationSession;

    std::vector<std::unique_ptr<SessionWrapper>> m_wrappers;

    bool m_lockOnDisconnect = false;

    KStatusNotifierItem *m_sni;
};
