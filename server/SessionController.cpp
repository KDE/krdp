// SPDX-FileCopyrightText: 2024 Arjen Hiemstra <ahiemstra@heimr.nl>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "SessionController.h"

#include <Cursor.h>
#include <InputHandler.h>
#include <KLocalizedString>
#include <PortalSession.h>
#include <QAction>
#include <QDBusInterface>
#include <RdpConnection.h>
#include <Server.h>

#ifdef WITH_PLASMA_SESSION
#include <PlasmaScreencastV1Session.h>
#endif

class SessionWrapper : public QObject
{
    Q_OBJECT
public:
    SessionWrapper(KRdp::Server *server, KRdp::RdpConnection *conn, bool usePlasmaSession, KStatusNotifierItem *sni)
    {
#ifdef WITH_PLASMA_SESSION
        if (usePlasmaSession) {
            session = std::make_unique<KRdp::PlasmaScreencastV1Session>(server);
        } else
#endif
        {
            session = std::make_unique<KRdp::PortalSession>(server);
        }
        connection = conn;
        m_sni = sni;

        connect(session.get(), &KRdp::AbstractSession::frameReceived, connection->videoStream(), &KRdp::VideoStream::queueFrame);
        connect(session.get(), &KRdp::AbstractSession::cursorUpdate, this, &SessionWrapper::onCursorUpdate);
        connect(session.get(), &KRdp::AbstractSession::error, this, &SessionWrapper::sessionError);
        connect(connection->videoStream(), &KRdp::VideoStream::enabledChanged, this, &SessionWrapper::onVideoStreamEnabledChanged);
        connect(connection->videoStream(), &KRdp::VideoStream::requestedFrameRateChanged, this, &SessionWrapper::onRequestedFrameRateChanged);
        connect(connection->inputHandler(), &KRdp::InputHandler::inputEvent, session.get(), &KRdp::AbstractSession::sendEvent);
        connect(connection, &KRdp::RdpConnection::stateChanged, this, &SessionWrapper::setSNIStatus);
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

    void setSNIStatus()
    {
        if (!m_sni) {
            return;
        }
        if (!connection) {
            m_sni->setStatus(KStatusNotifierItem::Passive);
            return;
        }
        switch (connection->state()) {
        case KRdp::RdpConnection::State::Closed:
        case KRdp::RdpConnection::State::Initial:
            m_sni->setStatus(KStatusNotifierItem::Passive);
            break;
        case KRdp::RdpConnection::State::Starting:
        case KRdp::RdpConnection::State::Running:
        case KRdp::RdpConnection::State::Streaming:
            m_sni->setStatus(KStatusNotifierItem::Active);
            break;
        default:
            m_sni->setStatus(KStatusNotifierItem::Passive);
            break;
        }
    }

    Q_SIGNAL void sessionError();

    std::unique_ptr<KRdp::AbstractSession> session;
    QPointer<KRdp::RdpConnection> connection;
    KStatusNotifierItem *m_sni;
};

SessionController::SessionController(KRdp::Server *server)
    : m_server(server)
{
    connect(m_server, &KRdp::Server::newConnection, this, &SessionController::onNewConnection);
    // Status notification item
    m_sni = new KStatusNotifierItem(u"krdpserver"_qs, this);
    m_sni->setTitle(i18n("RDP Server"));
    m_sni->setIconByName(u"preferences-system-network-remote"_qs);
    m_sni->setStatus(KStatusNotifierItem::Passive);
    auto quitAction = new QAction(i18n("Quit"), this);
    connect(quitAction, &QAction::triggered, this, &SessionController::stopFromSNI);
    m_sni->addAction(u"quitAction"_qs, quitAction);
}

SessionController::~SessionController() noexcept
{
}

void SessionController::setUsePlasmaSession(bool plasma)
{
    m_usePlasmaSession = plasma;
}

void SessionController::setMonitorIndex(const std::optional<int> &index)
{
    m_monitorIndex = index;
}

void SessionController::setQuality(const std::optional<int> &quality)
{
    m_quality = quality;
}

void SessionController::onNewConnection(KRdp::RdpConnection *newConnection)
{
    auto wrapper = std::make_unique<SessionWrapper>(m_server, newConnection, m_usePlasmaSession, m_sni);
    wrapper->session->setActiveStream(m_monitorIndex.value_or(-1));
    wrapper->session->setVideoQuality(m_quality.value());

    connect(newConnection, &QObject::destroyed, this, [this, newConnection]() {
        removeConnection(newConnection);
    });

    connect(wrapper->session.get(), &KRdp::AbstractSession::error, this, [this, newConnection] {
        newConnection->close(KRdp::RdpConnection::CloseReason::None);
        removeConnection(newConnection);
    });

    m_wrappers.push_back(std::move(wrapper));
}

void SessionController::removeConnection(KRdp::RdpConnection *connection)
{
    m_wrappers.erase(std::remove_if(m_wrappers.begin(),
                                    m_wrappers.end(),
                                    [connection](std::unique_ptr<SessionWrapper> &wrapper) {
                                        return wrapper->connection == connection;
                                    }),
                     m_wrappers.end());
}

void SessionController::stopFromSNI()
{
    // Uses dbus to stop the server service, like in the KCM
    // This kills all krdpserver instances, like a "panic button"
    QDBusInterface unit(u"org.freedesktop.systemd1"_qs,
                        u"/org/freedesktop/systemd1/unit/plasma_2dkrdp_5fserver_2eservice"_qs,
                        u"org.freedesktop.systemd1.Unit"_qs);

    unit.asyncCall(u"Stop"_qs);
}

#include "SessionController.moc"
