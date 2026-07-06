// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>

#include <QObject>
#include <QPoint>
#include <QQueue>
#include <QRegion>
#include <QRect>
#include <QSize>

#include <PipeWireEncodedStream>
#include <PipeWireSourceStream>

#include "RdpGfxPipeline.h"
#include "VideoFrame.h"
#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;
/**
 * A class that encapsulates an RdpGfx video stream.
 *
 * Video streaming is done using the "RDP Graphics Pipeline" protocol
 * extension which allows using h264 as the codec for the video stream.
 * However, this protocol extension is fairly complex to setup and use.
 *
 * VideoStream makes sure to handle most of the complexity of the RdpGfx
 * protocol like ensuring the client knows the right resolution and a
 * surface at the right size. It also takes care of sending the frames,
 * using a separate thread for a submission queue.
 *
 * VideoStream is managed by Session. Each session will have one instance
 * of this class.
 */
class KRDP_EXPORT VideoStream : public QObject
{
    Q_OBJECT

public:
    explicit VideoStream(RdpConnection *session, RdpGfxPipeline *pipeline, const QRect &geometry);
    ~VideoStream() override;
    void close();
    Q_SIGNAL void closed();
    Q_SIGNAL void sizeChanged(const QSize &size);
    Q_SIGNAL void cursorChanged(const PipeWireCursor &cursor);

    /**
     * Queue a frame to be sent to the client.
     *
     * This will add the provided frame to the queue of frames that should
     * be sent to the client.
     *
     * \param frame The frame to send.
     */
    void queueFrame(const VideoFrame &frame);

    /**
     * Indicate that the video state should be reset.
     *
     * This means the screen resolution and other information of the client
     * will be updated based on the current state of the VideoStream.
     */
    void setStreamingEnabled(bool enabled);
    void setVideoQuality(quint8 quality);
    void setPipeWireSource(quint32 nodeId, int fd = -1);

private:
    void onPacketReceived(const PipeWireEncodedStream::Packet &data);
    void onFrameReceived(const PipeWireFrame &frame);
    void setActiveEncodingMode(RdpGfxPipeline::EncodingMode mode);
    void clearSurface();
    void sendFrame(const VideoFrame &frame);

    void updateRequestedFrameRate();

    class Private;
    const std::unique_ptr<Private> d;
};

}
