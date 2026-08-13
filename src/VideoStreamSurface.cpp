// SPDX-FileCopyrightText: 2026 David Edmundson <kde@davidedmundson.co.uk>
// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoStreamSurface.h"

#include <chrono>

#include "krdp_logging.h"

namespace KRdp
{

namespace clk = std::chrono;

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
}
