// SPDX-FileCopyrightText: 2024 Arjen Hiemstra <ahiemstra@heimr.nl>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "SessionController.h"

#include <QAction>
#include <QCoreApplication>
#include <QDBusInterface>
#include <QMenu>

#include <KLocalizedString>

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

        connect(session.get(), &KRdp::AbstractSession::frameReceived, connection->videoStream(), &KRdp::VideoStream::queueFrame);
        connect(session.get(), &KRdp::AbstractSession::cursorUpdate, this, &SessionWrapper::onCursorUpdate);
        connect(session.get(), &KRdp::AbstractSession::error, this, &SessionWrapper::sessionError);
        connect(session.get(), &KRdp::AbstractSession::clipboardDataChanged, connection->clipboard(), &KRdp::Clipboard::setServerData);

        connect(connection->videoStream(), &KRdp::VideoStream::enabledChanged, this, &SessionWrapper::onVideoStreamEnabledChanged);
        connect(connection->videoStream(), &KRdp::VideoStream::requestedFrameRateChanged, this, &SessionWrapper::onRequestedFrameRateChanged);
        connect(connection->inputHandler(), &KRdp::InputHandler::inputEvent, session.get(), &KRdp::AbstractSession::sendEvent);
        connect(connection->clipboard(), &KRdp::Clipboard::clientDataChanged, session.get(), [clipboard = connection->clipboard(), this]() {
            session->setClipboardData(clipboard->getClipboard());
        });

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
        if (connection->videoStream()->enabled()) {
            session->requestStreamingEnable(connection->videoStream());
        } else {
            session->requestStreamingDisable(connection->videoStream());
        }
    }

    void onRequestedFrameRateChanged()
    {
        session->setVideoFrameRate(connection->videoStream()->requestedFrameRate());
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

void SessionController::onNewConnection(KRdp::RdpConnection *newConnection)
{
    auto wrapper = std::make_unique<SessionWrapper>(newConnection, makeSession(), m_sni);
    if (m_virtualMonitor) {
        wrapper->session->setVirtualMonitor(*m_virtualMonitor);
    } else {
        wrapper->session->setActiveStream(m_monitorIndex.value_or(-1));
    }
    wrapper->session->setVideoQuality(m_quality.value());

    connect(wrapper.get(), &SessionWrapper::connectionDestroyed, this, [this](SessionWrapper *wrapper) {
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

    wrapper->session->start();
    m_wrappers.push_back(std::move(wrapper));
}

void SessionController::stopFromSNI()
{
    // Uses dbus to stop the server service, like in the KCM
    // This kills all krdpserver instances, like a "panic button"
    QDBusInterface unit(u"org.freedesktop.systemd1"_s,
                        u"/org/freedesktop/systemd1/unit/plasma_2dkrdp_5fserver_2eservice"_s,
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
