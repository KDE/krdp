// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "VideoStream.h"
#include "krdp_export.h"

#include <PipeWireEncodedStream>
#include <PipeWireSourceStream>

namespace KRdp
{
class Server;

struct VirtualMonitor {
    QString name;
    QSize size;
    qreal dpr;
};

class KRDP_EXPORT AbstractSession : public QObject
{
    Q_OBJECT
public:
    AbstractSession();
    ~AbstractSession() override;

    /**
     * Properties have been initialised and we can start the session
     */
    virtual void start() = 0;

    bool streamingEnabled() const;
    void setStreamingEnabled(bool enable);
    void setVideoFrameRate(quint32 framerate);
    void setActiveStream(int stream);
    void setVirtualMonitor(const VirtualMonitor &vm);
    void setVideoQuality(quint8 quality);

    void requestStreamingEnable(QObject *requester);
    void requestStreamingDisable(QObject *requester);

    /**
     * Send a new event to the portal.
     *
     * \param event The new event to send.
     */
    virtual void sendEvent(const std::shared_ptr<QEvent> &event) = 0;

Q_SIGNALS:
    void started();
    void error();

    /**
     * Emitted whenever a new frame has been received.
     *
     * Received in this case means that the portal has sent the data and it has
     * been encoded by libav.
     */
    void frameReceived(const VideoFrame &frame);

    /**
     * Emitted whenever a new cursor update was received.
     *
     * These are separate from frames as RDP has a separate protocol for mouse
     * movement that is more performant than embedding things into the video
     * stream.
     */
    void cursorUpdate(const PipeWireCursor &cursor);

protected:
    QSize size() const;
    QSize logicalSize() const;
    std::optional<VirtualMonitor> virtualMonitor() const;
    int activeStream() const;

    void setStarted(bool started);
    void setSize(QSize size);
    void setLogicalSize(QSize size);
    PipeWireEncodedStream *stream();

private:
    class Private;
    const std::unique_ptr<Private> d;
};

}
