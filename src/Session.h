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

class KRDP_EXPORT Session : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Initial,
        Starting,
        Running,
        Closed,
    };

    explicit Session(Server *server, qintptr socketHandle);
    ~Session() override;

    State state() const;
    Q_SIGNAL void stateChanged();

    InputHandler *inputHandler() const;

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
