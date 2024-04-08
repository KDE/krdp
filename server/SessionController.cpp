// SPDX-FileCopyrightText: 2024 Arjen Hiemstra <ahiemstra@heimr.nl>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "SessionController.h"

#include <Cursor.h>
#include <InputHandler.h>
#include <PortalSession.h>
#include <RdpConnection.h>
#include <Server.h>

#ifdef WITH_PLASMA_SESSION
#include <PlasmaScreencastV1Session.h>
#endif

class SessionWrapper : public QObject
{
    Q_OBJECT
public:
    SessionWrapper(KRdp::Server *server, KRdp::RdpConnection *conn, bool usePlasmaSession)
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

        connect(session.get(), &KRdp::AbstractSession::frameReceived, connection->videoStream(), &KRdp::VideoStream::queueFrame);
        connect(session.get(), &KRdp::AbstractSession::cursorUpdate, this, &SessionWrapper::onCursorUpdate);
        connect(session.get(), &KRdp::AbstractSession::error, this, &SessionWrapper::sessionError);
        connect(connection->videoStream(), &KRdp::VideoStream::enabledChanged, this, &SessionWrapper::onVideoStreamEnabledChanged);
        connect(connection->videoStream(), &KRdp::VideoStream::requestedFrameRateChanged, this, &SessionWrapper::onRequestedFrameRateChanged);
        connect(connection->inputHandler(), &KRdp::InputHandler::inputEvent, session.get(), &KRdp::AbstractSession::sendEvent);
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

    Q_SIGNAL void sessionError();

    std::unique_ptr<KRdp::AbstractSession> session;
    QPointer<KRdp::RdpConnection> connection;
};

SessionController::SessionController(KRdp::Server *server)
    : m_server(server)
{
    connect(m_server, &KRdp::Server::newConnection, this, &SessionController::onNewConnection);
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
    auto wrapper = std::make_unique<SessionWrapper>(m_server, newConnection, m_usePlasmaSession);
    wrapper->session->setActiveStream(m_monitorIndex.value_or(-1));

    connect(newConnection, &KRdp::RdpConnection::stateChanged, this, [this, newConnection]() {
        if (newConnection->state() == KRdp::RdpConnection::State::Closed) {
            removeConnection(newConnection);
        }
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

#include "SessionController.moc"
