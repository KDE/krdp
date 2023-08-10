// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

#include <QDBusPendingCallWatcher>
#include <QObject>
#include <QPoint>
#include <QPointer>

#include <PipeWireEncodedStream>
#include <PipeWireSourceStream>

#include "krdp_export.h"

namespace KRdp
{

struct VideoFrame;
class Server;

/**
 * An implementation of the Plasma screencasting wayland protocol.
 */
class KRDP_EXPORT PlasmaSession : public QObject
{
    Q_OBJECT

public:
    PlasmaSession(Server *server);
    ~PlasmaSession();

    Q_SIGNAL void started();
    Q_SIGNAL void error();

    bool streamingEnabled() const;
    void setStreamingEnabled(bool enable);
    void setVideoFrameRate(quint32 framerate);
    void setActiveStream(int stream);

    /**
     * Send a new event to the portal.
     *
     * \param event The new event to send.
     */
    void sendEvent(QEvent *event);

    /**
     * Emitted whenever a new frame has been received.
     *
     * Received in this case means that the portal has sent the data and it has
     * been encoded by libav.
     */
    Q_SIGNAL void frameReceived(const VideoFrame &frame);

    /**
     * Emitted whenever a new cursor update was received.
     *
     * These are separate from frames as RDP has a separate protocol for mouse
     * movement that is more performant than embedding things into the video
     * stream.
     */
    Q_SIGNAL void cursorUpdate(const PipeWireCursor &cursor);

private:
    void onPacketReceived(const PipeWireEncodedStream::Packet &data);

    class Private;
    const std::unique_ptr<Private> d;
};

}
