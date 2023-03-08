// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <thread>

#include <QObject>

#include <freerdp/freerdp.h>

#include "krdp_export.h"

namespace KRdp
{

class InputHandler;
class Server;
class VideoStream;

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
class KRDP_EXPORT Session : public QObject
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
        Closed,
    };

    /**
     * Constructor.
     *
     * \param server The KRdp::Server instance this session is part of.
     * \param socketHandle A file handle to the socket this session should use
     *                     for communication.
     */
    explicit Session(Server *server, qintptr socketHandle);
    ~Session() override;

    /**
     * The current session state.
     */
    State state() const;
    Q_SIGNAL void stateChanged();

    /**
     * The InputHandler instance associated with this session.
     */
    InputHandler *inputHandler() const;
    /**
     * The VideoStream instance associated with this session.
     */
    VideoStream *videoStream() const;

private:
    friend BOOL peerCapabilities(freerdp_peer *);
    friend BOOL peerActivate(freerdp_peer *);
    friend BOOL peerPostConnect(freerdp_peer *);

    friend class VideoStream;

    void setState(State newState);
    void initialize();
    void run(std::stop_token stopToken);

    freerdp_peer *rdpPeer() const;

    bool onCapabilities();
    bool onActivate();
    bool onPostConnect();
    bool onClose();

    class Private;
    const std::unique_ptr<Private> d;
};

}
