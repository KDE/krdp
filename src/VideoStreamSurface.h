// SPDX-FileCopyrightText: 2026 David Edmundson <kde@davidedmundson.co.uk>
// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <optional>

#include <DmaBufHandler>
#include <PipeWireEncodedStream>
#include <PipeWireSourceStream>
#include <freerdp/codec/progressive.h>
#include <freerdp/server/rdpgfx.h>

#include <QQueue>

#include "VideoStream.h"

namespace KRdp
{

struct Surface {
    uint16_t id;
    uint32_t codecContextId;
    QSize size;
};

class VideoStreamSurface : public QObject
{
public:
    explicit VideoStreamSurface(VideoStream *stream);

    void setActiveEncodingMode(VideoStream::EncodingMode mode, quint8 quality, int requestedFrameRate);
    void setStreamingEnabled(bool enabled);
    void setVideoQuality(quint8 quality);
    void setRequestedSize(const QSize &size);
    void setPipeWireSource(quint32 nodeId, quint64 objectSerial, int fd);

    void queueFrame(const VideoFrame &frame);
    // This can be called from the submission thread
    bool hasNextFrame() const;
    void onPacketReceived(const PipeWireEncodedStream::Packet &data);
    void onFrameReceived(const PipeWireFrame &data);

    // This can be called from the submission thread
    bool sendFrameH264(RdpgfxServerContext *gfxContext, uint32_t frameId);

    // This can be called from the submission thread
    bool sendFrameProgressive(RdpgfxServerContext *gfxContext, PROGRESSIVE_CONTEXT *progressive, uint32_t frameId);

    std::unique_ptr<PipeWireEncodedStream> encodedStream;
    std::unique_ptr<PipeWireSourceStream> sourceStream;
    DmaBufHandler dmaBufHandler;
    quint32 nodeId = 0;
    int pipeWireFd = -1;
    quint64 objectSerial = quint64(-1);
    Surface surface;
    QSize size;
    QSize requestedSize;
    bool pendingReset = true;

private:
    void updateBackpressure();
    VideoFrame takeNextFrame();

    VideoStream *const m_stream;
    std::optional<VideoStream::EncodingMode> m_encodingMode;
    mutable std::mutex m_frameQueueMutex;
    QQueue<VideoFrame> m_frameQueue;
};
}
