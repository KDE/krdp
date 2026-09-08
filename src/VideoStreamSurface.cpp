// SPDX-FileCopyrightText: 2026 David Edmundson <kde@davidedmundson.co.uk>
// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoStreamSurface.h"

#include <chrono>
#include <optional>

#include <QDateTime>

#include "krdp_logging.h"

namespace KRdp
{

namespace clk = std::chrono;

static RECTANGLE_16 toRectangle16(const QRect &rect)
{
    RECTANGLE_16 result = {};
    result.left = rect.left();
    result.top = rect.top();
    result.right = rect.right() + 1;
    result.bottom = rect.bottom() + 1;
    return result;
}

static std::optional<REGION16> toRegion16(const QRegion &region, const QRect &frameRect)
{
    REGION16 invalidRegion = {};
    region16_init(&invalidRegion);

    const QRegion clipped = region.isEmpty() ? QRegion(frameRect) : region.intersected(frameRect);
    for (const QRect &rect : clipped) {
        if (!rect.isValid()) {
            continue;
        }

        const RECTANGLE_16 rectangle = toRectangle16(rect);
        if (!region16_union_rect(&invalidRegion, &invalidRegion, &rectangle)) {
            region16_uninit(&invalidRegion);
            return std::nullopt;
        }
    }

    if (region16_is_empty(&invalidRegion)) {
        const RECTANGLE_16 fullFrame = toRectangle16(frameRect);
        if (!region16_union_rect(&invalidRegion, &invalidRegion, &fullFrame)) {
            region16_uninit(&invalidRegion);
            return std::nullopt;
        }
    }

    return invalidRegion;
}

VideoStreamSurface::VideoStreamSurface(VideoStream *stream)
    : QObject(stream)
    , m_stream(stream)
{
}

void VideoStreamSurface::setActiveEncodingMode(VideoStream::EncodingMode mode, quint8 quality, int requestedFrameRate)
{
    if (encodedStream) {
        encodedStream->stop();
        encodedStream.reset();
    }
    if (sourceStream) {
        sourceStream->setActive(false);
        sourceStream.reset();
    }

    if (mode == VideoStream::EncodingMode::H264) {
        encodedStream = std::make_unique<PipeWireEncodedStream>();
        encodedStream->setEncodingPreference(PipeWireBaseEncodedStream::EncodingPreference::Speed);
        encodedStream->setColorRange(PipeWireBaseEncodedStream::ColorRange::Full);
        encodedStream->setEncoder(PipeWireEncodedStream::H264Baseline);
        encodedStream->setQuality(quality);
        encodedStream->setMaxFramerate(requestedFrameRate, 1);
        encodedStream->setMaxPendingFrames(requestedFrameRate);
        if (requestedSize.isValid()) {
            encodedStream->setRequestedSize(requestedSize);
        }

        QObject::connect(encodedStream.get(), &PipeWireEncodedStream::newPacket, this, [this](const auto &packet) {
            onPacketReceived(packet);
        });
        QObject::connect(encodedStream.get(), &PipeWireEncodedStream::sizeChanged, this, [this](const QSize &newSize) {
            m_stream->setSize(newSize);
        });
        QObject::connect(encodedStream.get(), &PipeWireEncodedStream::cursorChanged, m_stream, &VideoStream::cursorChanged);
        if (nodeId != 0) {
            encodedStream->setObjectSerial(objectSerial);
            encodedStream->setNodeId(nodeId);
            if (pipeWireFd > 0) {
                encodedStream->setFd(pipeWireFd);
            }
        }
        if (m_stream->streamingEnabled() && nodeId != 0) {
            encodedStream->start();
        }
    } else {
        sourceStream = std::make_unique<PipeWireSourceStream>();
        sourceStream->setAllowDmaBuf(true);
        sourceStream->setDamageEnabled(true);
        sourceStream->setMaxFramerate({static_cast<quint32>(requestedFrameRate), 1});
        if (requestedSize.isValid()) {
            sourceStream->setRequestedSize(requestedSize);
        }
        QObject::connect(
            sourceStream.get(),
            &PipeWireSourceStream::frameReceived,
            this,
            [this](const auto &frame) {
                onFrameReceived(frame);
            },
            Qt::QueuedConnection);
        QObject::connect(sourceStream.get(), &PipeWireSourceStream::streamParametersChanged, this, [this]() {
            m_stream->setSize(sourceStream->size());
        });
        QObject::connect(
            sourceStream.get(),
            &PipeWireSourceStream::frameReceived,
            this,
            [this](const PipeWireFrame &frame) {
                if (frame.cursor) {
                    Q_EMIT m_stream->cursorChanged(*frame.cursor);
                }
            },
            Qt::QueuedConnection);

        if (nodeId != 0 && pipeWireFd) {
            bool created = false;
            if (objectSerial != quint64(-1)) {
                created = sourceStream->createStream(objectSerial, pipeWireFd);
            } else {
                created = sourceStream->createStream(nodeId, pipeWireFd);
            }
            if (!created) {
                qCWarning(KRDP) << "Could not create PipeWire source stream" << sourceStream->error();
                m_stream->failVideoInitialization();
                return;
            }
            size = sourceStream->size();
        }
        sourceStream->setActive(m_stream->streamingEnabled() && nodeId != 0);
    }
}

void VideoStreamSurface::queueFrame(const VideoFrame &frame)
{
    m_stream->queueFrame(frame);
}

void VideoStreamSurface::setStreamingEnabled(bool enabled)
{
    if (encodedStream) {
        if (enabled && nodeId != 0) {
            if (encodedStream->state() == PipeWireBaseEncodedStream::Paused) {
                encodedStream->resume();
            } else {
                encodedStream->start();
            }
        } else {
            encodedStream->pause();
        }
    }
    if (sourceStream) {
        sourceStream->setActive(enabled && nodeId != 0);
    }
}

void VideoStreamSurface::setVideoQuality(quint8 quality)
{
    if (encodedStream) {
        encodedStream->setQuality(quality);
    }
}

void VideoStreamSurface::setRequestedSize(const QSize &newSize)
{
    requestedSize = newSize;
    if (encodedStream) {
        encodedStream->setRequestedSize(newSize);
    }
    if (sourceStream) {
        sourceStream->setRequestedSize(newSize);
    }
}

void VideoStreamSurface::setPipeWireSource(quint32 newNodeId, quint64 newObjectSerial, int fd)
{
    nodeId = newNodeId;
    objectSerial = newObjectSerial;
    pipeWireFd = fd;

    if (encodedStream) {
        encodedStream->setObjectSerial(objectSerial);
        encodedStream->setNodeId(nodeId);
        encodedStream->setFd(pipeWireFd);
        if (m_stream->streamingEnabled()) {
            encodedStream->start();
        }
    }

    if (!sourceStream) {
        return;
    }

    bool created = false;
    if (objectSerial != quint64(-1)) {
        created = sourceStream->createStream(objectSerial, fd);
    } else {
        created = sourceStream->createStream(nodeId, fd);
    }

    if (!created) {
        qCWarning(KRDP) << "Could not create PipeWire source stream" << sourceStream->error();
        m_stream->failVideoInitialization();
        return;
    }

    m_stream->setSize(sourceStream->size());
    sourceStream->setActive(m_stream->streamingEnabled());
}

void VideoStreamSurface::onPacketReceived(const PipeWireEncodedStream::Packet &data)
{
    VideoFrame frameData;
    frameData.size = size;
    frameData.data = data.data();
    frameData.isKeyFrame = data.isKeyFrame();
    queueFrame(frameData);
}

void VideoStreamSurface::onFrameReceived(const PipeWireFrame &data)
{
    VideoFrame frameData;
    frameData.size = data.dataFrame ? data.dataFrame->size : QSize(data.dmabuf ? data.dmabuf->width : 0, data.dmabuf ? data.dmabuf->height : 0);
    frameData.damage = data.damage.value_or(QRegion(QRect(QPoint(0, 0), frameData.size)));
    if (data.presentationTimestamp) {
        frameData.presentationTimeStamp = clk::system_clock::time_point(clk::duration_cast<clk::microseconds>(*data.presentationTimestamp));
    }

    if (data.dataFrame) {
        frameData.image = data.dataFrame->toImage().convertToFormat(QImage::Format_RGB32);
    } else if (data.dmabuf) {
        QImage image(frameData.size, QImage::Format_RGBA8888_Premultiplied);
        if (!dmaBufHandler.downloadFrame(image, data)) {
            qCWarning(KRDP) << "Failed to download DMA-BUF frame";
            return;
        }
        frameData.image = std::move(image);
    } else {
        qCWarning(KRDP) << "PipeWire frame did not contain usable image data";
        return;
    }

    queueFrame(frameData);
}

bool VideoStreamSurface::sendFrameH264(RdpgfxServerContext *gfxContext, uint32_t frameId, const VideoFrame &frame)
{
    if (frame.data.isEmpty()) {
        return false;
    }

    if (surface.id == 0) {
        qCWarning(KRDP) << "No graphics surface available for H264 frame submission";
        return false;
    }

    RDPGFX_START_FRAME_PDU startFramePdu = {};
    RDPGFX_END_FRAME_PDU endFramePdu = {};

    const auto now = QDateTime::currentDateTimeUtc().time();
    startFramePdu.timestamp = now.hour() << 22 | now.minute() << 16 | now.second() << 10 | now.msec();
    startFramePdu.frameId = frameId;
    endFramePdu.frameId = frameId;

    RDPGFX_SURFACE_COMMAND surfaceCommand = {};
    surfaceCommand.surfaceId = surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_AVC420;
    surfaceCommand.contextId = 0;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;
    surfaceCommand.right = frame.size.width();
    surfaceCommand.bottom = frame.size.height();
    surfaceCommand.width = frame.size.width();
    surfaceCommand.height = frame.size.height();

    RDPGFX_AVC420_BITMAP_STREAM avcStream = {};
    surfaceCommand.extra = &avcStream;
    avcStream.data = reinterpret_cast<BYTE *>(const_cast<char *>(frame.data.data()));
    avcStream.length = frame.data.length();

    avcStream.meta.numRegionRects = 1;
    RECTANGLE_16 rect = {0, 0, static_cast<UINT16>(frame.size.width()), static_cast<UINT16>(frame.size.height())};
    avcStream.meta.regionRects = &rect;
    RDPGFX_H264_QUANT_QUALITY quality = {22, 0, 100};
    avcStream.meta.quantQualityVals = &quality;

    const UINT startStatus = gfxContext->StartFrame(gfxContext, &startFramePdu);
    if (startStatus != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "StartFrame failed" << startStatus << "frameId" << frameId;
        return true;
    }

    const UINT commandStatus = gfxContext->SurfaceCommand(gfxContext, &surfaceCommand);
    if (commandStatus != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "SurfaceCommand failed" << commandStatus << "frameId" << frameId << "surface" << surface.id << "encodedBytes" << frame.data.size();
    }

    const UINT endStatus = gfxContext->EndFrame(gfxContext, &endFramePdu);
    if (endStatus != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "EndFrame failed" << endStatus << "frameId" << frameId;
    }

    return true;
}

bool VideoStreamSurface::sendFrameProgressive(RdpgfxServerContext *gfxContext, PROGRESSIVE_CONTEXT *progressive, uint32_t frameId, const VideoFrame &frame)
{
    if (frame.image.isNull()) {
        return false;
    }

    if (surface.id == 0) {
        qCWarning(KRDP) << "No graphics surface available for progressive frame submission";
        return false;
    }

    QImage image = frame.image.convertToFormat(QImage::Format_RGB32);
    const QRect frameRect(QPoint(0, 0), image.size());
    auto invalidRegion = toRegion16(frame.damage, frameRect);
    if (!invalidRegion) {
        qCWarning(KRDP) << "Failed to build invalid region for progressive frame";
        return false;
    }

    BYTE *encodedData = nullptr;
    UINT32 encodedSize = 0;
    const UINT32 rectCount = region16_n_rects(&*invalidRegion);
    const int compressionStatus = progressive_compress(progressive,
                                                       image.constBits(),
                                                       image.sizeInBytes(),
                                                       PIXEL_FORMAT_BGRX32,
                                                       image.width(),
                                                       image.height(),
                                                       image.bytesPerLine(),
                                                       &*invalidRegion,
                                                       &encodedData,
                                                       &encodedSize);
    if (compressionStatus < 0 || !encodedData || encodedSize == 0) {
        region16_uninit(&*invalidRegion);
        qCWarning(KRDP) << "Failed to compress progressive frame"
                        << "status" << compressionStatus << "rects" << rectCount << "size" << frame.size;
        return false;
    }

    RDPGFX_START_FRAME_PDU startFramePdu = {};
    RDPGFX_END_FRAME_PDU endFramePdu = {};

    const auto now = QDateTime::currentDateTimeUtc().time();
    startFramePdu.timestamp = now.hour() << 22 | now.minute() << 16 | now.second() << 10 | now.msec();
    startFramePdu.frameId = frameId;
    endFramePdu.frameId = frameId;

    const RECTANGLE_16 *extents = region16_extents(&*invalidRegion);
    RDPGFX_SURFACE_COMMAND surfaceCommand = {};
    surfaceCommand.surfaceId = surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_CAPROGRESSIVE;
    surfaceCommand.contextId = surface.codecContextId;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;
    surfaceCommand.left = extents->left;
    surfaceCommand.top = extents->top;
    surfaceCommand.right = extents->right;
    surfaceCommand.bottom = extents->bottom;
    surfaceCommand.width = frame.size.width();
    surfaceCommand.height = frame.size.height();
    surfaceCommand.length = encodedSize;
    surfaceCommand.data = encodedData;

    const UINT status = gfxContext->SurfaceFrameCommand(gfxContext, &surfaceCommand, &startFramePdu, &endFramePdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "SurfaceFrameCommand failed" << status << "frameId" << frameId << "surface" << surface.id << "encodedBytes" << encodedSize
                        << "damageRects" << rectCount;
    }

    region16_uninit(&*invalidRegion);
    return true;
}
}
