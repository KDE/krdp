// SPDX-FileCopyrightText: 2024 Arjen Hiemstra <ahiemstra@heimr.nl>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "SessionController.h"
#include "DisplayControl.h"
#include "VideoStream.h"

#include <QAction>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QMenu>

#include <KLocalizedString>

#include <PipeWireSourceStream>

#include <Clipboard.h>
#include <Cursor.h>
#include <InputHandler.h>
#include <PortalSession.h>
#include <RdpConnection.h>
#include <Server.h>

#ifdef WITH_PLASMA_SESSION
#include <PlasmaScreencastV1Session.h>
#endif

#include "VideoStream.h"

using namespace Qt::StringLiterals;

class SessionWrapper : public QObject
{
    Q_OBJECT
public:
    SessionWrapper(KRdp::RdpConnection *conn, std::unique_ptr<KRdp::AbstractSession> &&sess, KStatusNotifierItem *sni)
        : session(std::move(sess))
        , connection(conn)
    {
        m_sni = sni;

        connect(session.get(), &KRdp::AbstractSession::error, this, &SessionWrapper::sessionError);
        connect(session.get(), &KRdp::AbstractSession::started, this, &SessionWrapper::onSessionStarted);
        connect(session.get(), &KRdp::AbstractSession::clipboardDataChanged, connection->clipboard(), &KRdp::Clipboard::setServerData);

        connect(connection->videoStream(), &KRdp::VideoStream::cursorChanged, this, &SessionWrapper::onCursorUpdate);
        connect(connection->videoStream(), &KRdp::VideoStream::sizeChanged, session.get(), &KRdp::AbstractSession::setSize);
        connect(connection->videoStream(), &KRdp::VideoStream::enabledChanged, this, &SessionWrapper::onVideoStreamEnabledChanged);
        connect(connection->inputHandler(), &KRdp::InputHandler::inputEvent, session.get(), &KRdp::AbstractSession::sendEvent);
        connect(connection->clipboard(), &KRdp::Clipboard::clientDataChanged, session.get(), [clipboard = connection->clipboard(), this]() {
            session->setClipboardData(clipboard->getClipboard());
        });
        connect(connection->displayControl(), &KRdp::DisplayControl::requestedScreenSizeChanged, connection->videoStream(), &KRdp::VideoStream::setRequestedSize);

        connect(connection, &QObject::destroyed, this, &SessionWrapper::onConnectionDestroyed);
    }

    void onCursorUpdate(const PipeWireCursor &cursor)
    {
        if (!connection) {
            return;
        }

        KRdp::Cursor::CursorUpdate update;
        update.hotspot = cursor.hotspot;
        update.image = cursor.texture;
        connection->cursor()->update(update);
    }

    void onVideoStreamEnabledChanged()
    {
        connection->videoStream()->setStreamingEnabled(m_sessionStarted && connection->videoStream()->enabled());
    }

    void onSessionStarted()
    {
        m_sessionStarted = true;
        connection->videoStream()->setPipeWireSource(session->nodeId(), session->objectSerial(), session->takePipeWireFd());
        connection->videoStream()->setStreamingEnabled(connection->videoStream()->enabled());
    }

    void onConnectionDestroyed()
    {
        Q_EMIT connectionDestroyed(this);
    }

    Q_SIGNAL void sessionError();
    Q_SIGNAL void connectionDestroyed(SessionWrapper *wrapper);

    std::unique_ptr<KRdp::AbstractSession> session;
    QPointer<KRdp::RdpConnection> connection;
    KStatusNotifierItem *m_sni;
    bool m_sessionStarted = false;
};

SessionController::SessionController(KRdp::Server *server, SessionType sessionType)
    : m_server(server)
    , m_sessionType(sessionType)
{
    connect(m_server, &KRdp::Server::newConnectionCreated, this, &SessionController::onNewConnection);
    // Status notification item
    m_sni = new KStatusNotifierItem(u"krdpserver"_s, this);
    auto menu = new QMenu(u"quitMenu"_s);
    // Disable default quit button since it has confirmation dialog
    m_sni->setStandardActionsEnabled(false);
    m_sni->setTitle(i18n("RDP Server"));
    m_sni->setIconByName(u"preferences-system-network-remote"_s);
    m_sni->setStatus(KStatusNotifierItem::Passive);
    auto quitAction = new QAction(i18n("Quit"), menu);
    quitAction->setIcon(QIcon::fromTheme(QStringLiteral("application-exit")));
    connect(quitAction, &QAction::triggered, this, &SessionController::stopFromSNI);
    menu->addAction(quitAction);
    m_sni->setContextMenu(menu);
}

SessionController::~SessionController() noexcept
{
}

void SessionController::setMonitorIndex(const std::optional<int> &index)
{
    m_monitorIndex = index;
}

void SessionController::setVirtualMonitor(const KRdp::VirtualMonitor &virtualMonitor)
{
    m_virtualMonitor = virtualMonitor;
}

void SessionController::setQuality(const std::optional<int> &quality)
{
    m_quality = quality;
}

void SessionController::setLockOnDisconnect(bool lock)
{
    m_lockOnDisconnect = lock;
}

void SessionController::setSessionLocked(bool locked)
{
    if (!m_lockOnDisconnect) {
        return;
    }
    // Ask logind to lock/unlock the graphical session (kscreenlocker honours its Lock/Unlock
    // signals). krdpserver is a user-service process not in a login session, so resolve the
    // session from $XDG_SESSION_ID, falling back to our PID.
    //
    // Everything is done asynchronously with QDBusMessage + QDBusConnection::asyncCall (NOT
    // QDBusInterface, whose constructor blocks on introspection): a slow or hung logind must
    // never stall the GUI thread, which also drives PipeWire/input/video for every other
    // connected client (a blocking call could stall it up to the 25s D-Bus timeout).
    auto bus = QDBusConnection::systemBus();
    const QString service = u"org.freedesktop.login1"_s;
    const QString managerPath = u"/org/freedesktop/login1"_s;
    const QString managerIface = u"org.freedesktop.login1.Manager"_s;

    // Phase two: Lock/Unlock the resolved session object.
    auto lockSession = [this, locked, bus, service](const QDBusObjectPath &path) {
        QDBusMessage msg =
            QDBusMessage::createMethodCall(service, path.path(), u"org.freedesktop.login1.Session"_s, locked ? u"Lock"_s : u"Unlock"_s);
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [locked](QDBusPendingCallWatcher *self) {
            const QDBusPendingReply<> reply = *self;
            if (reply.isError()) {
                qWarning() << "krdp: could not" << (locked ? "lock" : "unlock") << "the logind session:" << reply.error().message();
            }
            self->deleteLater();
        });
    };

    // Phase one fallback: resolve the session by our PID.
    auto resolveByPid = [this, locked, bus, service, managerPath, managerIface, lockSession]() {
        QDBusMessage msg = QDBusMessage::createMethodCall(service, managerPath, managerIface, u"GetSessionByPID"_s);
        msg.setArguments({static_cast<quint32>(QCoreApplication::applicationPid())});
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [locked, lockSession](QDBusPendingCallWatcher *self) {
            const QDBusPendingReply<QDBusObjectPath> reply = *self;
            if (reply.isError()) {
                qWarning() << "krdp: could not resolve a logind session to" << (locked ? "lock" : "unlock") << ":" << reply.error().message();
            } else {
                lockSession(reply.value());
            }
            self->deleteLater();
        });
    };

    // Phase one: resolve by $XDG_SESSION_ID, else fall back to PID.
    const QString sessionId = qEnvironmentVariable("XDG_SESSION_ID");
    if (!sessionId.isEmpty()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(service, managerPath, managerIface, u"GetSession"_s);
        msg.setArguments({sessionId});
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [resolveByPid, lockSession](QDBusPendingCallWatcher *self) {
            const QDBusPendingReply<QDBusObjectPath> reply = *self;
            if (reply.isError()) {
                resolveByPid();
            } else {
                lockSession(reply.value());
            }
            self->deleteLater();
        });
    } else {
        resolveByPid();
    }
}

void SessionController::onNewConnection(KRdp::RdpConnection *newConnection)
{
    auto wrapper = std::make_unique<SessionWrapper>(newConnection, makeSession(), m_sni);
    if (m_virtualMonitor) {
        wrapper->session->setVirtualMonitor(*m_virtualMonitor);
    } else if (m_monitorIndex) {
        wrapper->session->setActiveStream(*m_monitorIndex);
    }
    wrapper->connection->videoStream()->setVideoQuality(m_quality.value());
    // Unlock only once the connection is authenticated and activated - NOT here, which
    // runs at TCP accept before the RDP handshake / PAM auth. Otherwise anyone who can
    // open the port could unlock the physical seat (logind Unlock is passwordless).
    connect(newConnection, &KRdp::RdpConnection::stateChanged, this, [this](KRdp::RdpConnection::State state) {
        if (state == KRdp::RdpConnection::State::Activated || state == KRdp::RdpConnection::State::Streaming) {
            setSessionLocked(false);
        }
    });
    wrapper->session->start();

    connect(wrapper.get(), &SessionWrapper::connectionDestroyed, this, [this](SessionWrapper *wrapper) {
        // Lock the machine as the last client leaves.
        if (m_wrappers.size() == 1 && m_wrappers.front().get() == wrapper) {
            setSessionLocked(true);
        }

        m_wrappers.erase(std::remove_if(m_wrappers.begin(),
                                        m_wrappers.end(),
                                        [wrapper](std::unique_ptr<SessionWrapper> &entry) {
                                            return entry.get() == wrapper;
                                        }),
                         m_wrappers.end());
    });

    connect(wrapper.get(), &SessionWrapper::sessionError, this, [newConnection] {
        newConnection->close(KRdp::RdpConnection::CloseReason::None);
    });

    m_wrappers.push_back(std::move(wrapper));
}

void SessionController::stopFromSNI()
{
    // Uses dbus to stop the server service, like in the KCM
    // This kills all krdpserver instances, like a "panic button"
    QDBusInterface unit(u"org.freedesktop.systemd1"_s,
                        u"/org/freedesktop/systemd1/unit/app_2dorg_2ekde_2ekrdpserver_2eservice"_s,
                        u"org.freedesktop.systemd1.Unit"_s);

    unit.asyncCall(u"Stop"_s);
    QCoreApplication::quit();
}

std::unique_ptr<KRdp::AbstractSession> SessionController::makeSession()
{
#ifdef WITH_PLASMA_SESSION
    if (m_sessionType == SessionType::Plasma) {
        return std::make_unique<KRdp::PlasmaScreencastV1Session>();
    } else
#endif
    {
        return std::make_unique<KRdp::PortalSession>();
    }
}

#include "SessionController.moc"
