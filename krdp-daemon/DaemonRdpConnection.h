// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <thread>

#include <QObject>

#include <freerdp/freerdp.h>

namespace KRdp
{

class InputHandler;
class DaemonServer;
class VideoStream;
class Cursor;
class NetworkDetection;
class Clipboard;
/*class DisplayControl;*/ // TODO RM

/**
 * An RDP session.
 *
 * This represents an RDP session, that is, a connection between an RDP client
 * and the server. It primarily takes care of the RDP communication side of
 * things.
 *
 * Note that this class starts its own thread for performing the actual
 * communication.
 */
class DaemonRdpConnection : public QObject
{
    Q_OBJECT

public:
    /**
     * Session state.
     */
    enum class State {
        Initial,
        Starting,
        Running,
        Activated,
        Streaming,
        Closed,
    };

    /**
     * Reasons for closing the stream.
     */
    enum class CloseReason {
        None, ///< No particular reason, e.g. closing due to normal operation
              ///  like client disconnect.
        VideoInitFailed, ///< VideoStream failed to initialize.
    };

    /**
     * Constructor.
     *
     * \param server The KRdp::Server instance this session is part of.
     * \param socketHandle A file handle to the socket this session should use
     *                     for communication.
     */
    explicit DaemonRdpConnection(DaemonServer *server, qintptr socketHandle);
    ~DaemonRdpConnection() override;

    /**
     * The current session state.
     */
    State state() const;
    Q_SIGNAL void stateChanged(State newState);

    /**
     * Close the connection
     *
     * \param reason The reason to close the connection. May set error state if
     *               it is something different than CloseReason::None.
     */
    void close(CloseReason reason = CloseReason::None);

    /**
     * Send an RDP server-redirection PDU containing \a redirectionToken.
     */
    void sendRedirection(const QString &redirectionToken);

    /**
     * The InputHandler instance associated with this session.
     */
    InputHandler *inputHandler() const;
    /**
     * The VideoStream instance associated with this session.
     */
    VideoStream *videoStream() const;
    /**
     * The Cursor instance associated with this session.
     */
    Cursor *cursor() const;

    Clipboard *clipboard() const;

    /*DisplayControl *displayControl() const;*/ // TODO RM

    NetworkDetection *networkDetection() const;
    freerdp_peer *rdpPeer() const;

private:
    friend BOOL peerCapabilities(freerdp_peer *);
    friend BOOL peerActivate(freerdp_peer *);
    friend BOOL peerPostConnect(freerdp_peer *);

    friend class Cursor;
    friend class VideoStream;
    friend class NetworkDetection;
    friend class Clipboard;
    /*friend class DisplayControl; */ // TODO RM

    void setState(State newState);
    void initialize();
    void run(std::stop_token stopToken);

    rdpContext *rdpPeerContext() const;

    bool onCapabilities();
    bool onActivate();
    bool onPostConnect();
    bool onClose();

    class Private;
    const std::unique_ptr<Private> d;
};

}
