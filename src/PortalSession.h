// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
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
 * A FreeDesktop Remote Desktop Portal session.
 *
 * This encapsulates all the required setup to start a FreeDesktop Remote
 * Desktop Portal session including input sending and video streaming.
 */
class KRDP_EXPORT PortalSession : public QObject
{
    Q_OBJECT

public:
    PortalSession(Server *server);
    ~PortalSession();

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
    void onCreateSession(uint code, const QVariantMap &result);
    void onDevicesSelected(uint code, const QVariantMap &result);
    void onSourcesSelected(uint code, const QVariantMap &result);
    void onSessionStarted(uint code, const QVariantMap &result);
    void onPacketReceived(const PipeWireEncodedStream::Packet &data);

    class Private;
    const std::unique_ptr<Private> d;
};

}
