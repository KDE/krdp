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

void SessionController::switchToGreeter()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DisplayManager"),
                                                          QStringLiteral("/org/freedesktop/DisplayManager/Seat0"),
                                                          QStringLiteral("org.freedesktop.DisplayManager.Seat"),
                                                          QStringLiteral("SwitchToGreeter"));

    QDBusPendingCall pendingCall = QDBusConnection::systemBus().asyncCall(message);

    auto *watcher = new QDBusPendingCallWatcher(pendingCall);

    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher]() {
        QDBusPendingReply<> reply = *watcher;

        if (reply.isError()) {
            qWarning() << "Failed to switch to greeter:" << reply.error().name() << reply.error().message();
        } else {
            qDebug() << "Switched to greeter successfully.";
        }

        watcher->deleteLater();
    });
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

ServerConfig::OperationMode SessionController::operationMode() const
{
    return m_operatingMode;
}

void SessionController::setOperationMode(ServerConfig::OperationMode mode)
{
    m_operatingMode = mode;
}

void SessionController::setSessionLocked(bool locked)
{
    auto bus = QDBusConnection::systemBus();
    const QString service = u"org.freedesktop.login1"_s;

    QDBusMessage lockMsg = QDBusMessage::createMethodCall(service,
                                                          u"/org/freedesktop/login1/session/auto"_s,
                                                          u"org.freedesktop.login1.Session"_s,
                                                          locked ? u"Lock"_s : u"Unlock"_s);
    auto *lockWatcher = new QDBusPendingCallWatcher(bus.asyncCall(lockMsg), this);
    connect(lockWatcher, &QDBusPendingCallWatcher::finished, this, [locked](QDBusPendingCallWatcher *self) {
        const QDBusPendingReply<> reply = *self;
        if (reply.isError()) {
            qWarning() << "krdp: could not" << (locked ? "lock" : "unlock") << "the logind session:" << reply.error().message();
        }
        self->deleteLater();
    });
}

void SessionController::onNewConnection(KRdp::RdpConnection *newConnection)
{
    connect(newConnection, &KRdp::RdpConnection::stateChanged, this, [this, newConnection](KRdp::RdpConnection::State state) {
        // RDP authentication has completed by the time the connection is activated.
        // Do not request a screencast session for clients that fail authentication.
        if (state != KRdp::RdpConnection::State::Activated) {
            return;
        }

        auto wrapper = std::make_unique<SessionWrapper>(newConnection, makeSession(), m_sni);
        if (m_virtualMonitor) {
            wrapper->session->setVirtualMonitor(*m_virtualMonitor);
        } else if (m_monitorIndex) {
            wrapper->session->setActiveStream(*m_monitorIndex);
        } else {
            // without an explicit override, we follow the operating mode
            KRdp::VirtualMonitor defaultMonitor;
            defaultMonitor.name = i18n("Remote Monitor");
            defaultMonitor.dpr = 1;
            defaultMonitor.size = QSize(1024, 768);
            switch (m_operatingMode) {
            case ServerConfig::SharedAccess:
                // we do not set a virtual monitor, allowing the client to see all the existing session.
                break;
            case ServerConfig::RemoteAccess:
                wrapper->session->setVirtualMonitor(defaultMonitor);
                // DAVE I kinda feel this shouldn't be here, we should message kwin which should then do this internally
                // we also want something to prevent a VT switch breaking things
                switchToGreeter();
                setSessionLocked(false);
                break;
            case ServerConfig::AdditionalDisplay:
                wrapper->session->setVirtualMonitor(defaultMonitor);
                break;
            }
        }

        wrapper->connection->videoStream()->setVideoQuality(m_quality.value());
        connect(wrapper.get(), &SessionWrapper::connectionDestroyed, this, [this](SessionWrapper *wrapper) {
            m_wrappers.erase(std::remove_if(m_wrappers.begin(),
                                            m_wrappers.end(),
                                            [wrapper](std::unique_ptr<SessionWrapper> &entry) {
                                                return entry.get() == wrapper;
                                            }),
                             m_wrappers.end());

            if (m_wrappers.empty() && m_operatingMode == ServerConfig::RemoteAccess) {
                setSessionLocked(true);
            }
        });

        connect(wrapper.get(), &SessionWrapper::sessionError, this, [newConnection] {
            newConnection->close(KRdp::RdpConnection::CloseReason::None);
        });

        wrapper->session->start();
        m_wrappers.push_back(std::move(wrapper));
    });
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
