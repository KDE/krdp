// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "VideoStream.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <numeric>

#include "NetworkDetection.h"
#include "RdpConnection.h"
#include "krdp_logging.h"

#include <KPipeWire/DmaBufHandler>

namespace KRdp
{

namespace
{
using clk = std::chrono::system_clock;
constexpr auto FrameRateEstimateAveragePeriod = std::chrono::seconds(1);

struct FrameRateEstimate {
    clk::system_clock::time_point timeStamp;
    int estimate = 0;
};
}

class KRDP_NO_EXPORT VideoStream::Private
{
public:
    RdpConnection *session = nullptr;
    RdpGfxPipeline *pipeline = nullptr;
    QRect geometry;
    std::optional<RdpGfxPipeline::EncodingMode> activeEncodingMode;
    std::unique_ptr<PipeWireEncodedStream> encodedStream;
    std::unique_ptr<PipeWireSourceStream> sourceStream;
    DmaBufHandler dmaBufHandler;
    RdpGfxPipeline::Surface surface;

    quint32 nodeId = 0;
    int pipeWireFd = -1;
    QSize size;

    bool streamingEnabled = false;

    std::jthread frameSubmissionThread;
    std::mutex frameQueueMutex;
    QQueue<VideoFrame> frameQueue;

    int maximumFrameRate = 120;
    std::atomic_int requestedFrameRate = 60;
    QQueue<FrameRateEstimate> frameRateEstimates;
    clk::system_clock::time_point lastFrameRateEstimation;
    quint8 quality = 100;
    bool loggedFirstFrame = false;

    void setSize(VideoStream *q, const QSize &newSize)
    {
        if (size == newSize) {
            return;
        }

        size = newSize;
        Q_EMIT q->sizeChanged(newSize);
    }
};

VideoStream::VideoStream(RdpConnection *session, RdpGfxPipeline *pipeline, const QRect &geometry)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
    d->pipeline = pipeline;
    d->geometry = geometry;

    connect(d->pipeline, &RdpGfxPipeline::encodingModeChanged, this, &VideoStream::setActiveEncodingMode);
    connect(d->pipeline, &RdpGfxPipeline::surfacesInvalidated, this, &VideoStream::clearSurface);
    connect(d->session->networkDetection(), &NetworkDetection::rttChanged, this, &VideoStream::updateRequestedFrameRate);

    if (auto mode = d->pipeline->encodingMode()) {
        setActiveEncodingMode(*mode);
    }

    d->frameSubmissionThread = std::jthread([this](std::stop_token token) {
        while (!token.stop_requested()) {
            if (!d->pipeline->hasInFlightCapacity() || !d->activeEncodingMode) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            VideoFrame nextFrame;
            {
                std::unique_lock lock(d->frameQueueMutex);
                if (!d->frameQueue.isEmpty()) {
                    nextFrame = d->frameQueue.takeFirst();
                }
            }

            if (nextFrame.size.isEmpty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000) / d->requestedFrameRate.load());
                continue;
            }

            sendFrame(nextFrame);
        }
    });
}

VideoStream::~VideoStream()
{
    close();
}

void VideoStream::setActiveEncodingMode(RdpGfxPipeline::EncodingMode mode)
{
    if (d->activeEncodingMode == mode) {
        return;
    }

    if (d->encodedStream) {
        d->encodedStream->stop();
        d->encodedStream.reset();
    }
    if (d->sourceStream) {
        d->sourceStream->setActive(false);
        d->sourceStream.reset();
    }

    {
        std::lock_guard lock(d->frameQueueMutex);
        d->frameQueue.clear();
    }

    d->pipeline->destroySurface(d->surface);
    d->activeEncodingMode = mode;

    if (mode == RdpGfxPipeline::EncodingMode::H264) {
        d->encodedStream = std::make_unique<PipeWireEncodedStream>();
        d->encodedStream->setEncodingPreference(PipeWireBaseEncodedStream::EncodingPreference::Speed);
        d->encodedStream->setColorRange(PipeWireBaseEncodedStream::ColorRange::Full);
        d->encodedStream->setEncoder(PipeWireEncodedStream::H264Baseline);
        d->encodedStream->setQuality(d->quality);
        d->encodedStream->setMaxFramerate(d->requestedFrameRate, 1);
        d->encodedStream->setMaxPendingFrames(d->requestedFrameRate);

        connect(d->encodedStream.get(), &PipeWireEncodedStream::newPacket, this, &VideoStream::onPacketReceived);
        connect(d->encodedStream.get(), &PipeWireEncodedStream::sizeChanged, this, [this](const QSize &size) {
            d->setSize(this, size);
        });
        connect(d->encodedStream.get(), &PipeWireEncodedStream::cursorChanged, this, &VideoStream::cursorChanged);

        if (d->nodeId != 0) {
            d->encodedStream->setNodeId(d->nodeId);
            if (d->pipeWireFd > 0) {
                d->encodedStream->setFd(d->pipeWireFd);
            }
        }
        if (d->streamingEnabled && d->nodeId != 0) {
            d->encodedStream->start();
        }
    } else {
        d->sourceStream = std::make_unique<PipeWireSourceStream>();
        d->sourceStream->setAllowDmaBuf(true);
        d->sourceStream->setDamageEnabled(true);
        d->sourceStream->setMaxFramerate(Fraction{static_cast<quint32>(d->requestedFrameRate.load()), 1});

        connect(d->sourceStream.get(), &PipeWireSourceStream::frameReceived, this, &VideoStream::onFrameReceived, Qt::QueuedConnection);
        connect(d->sourceStream.get(), &PipeWireSourceStream::streamParametersChanged, this, [this]() {
            d->setSize(this, d->sourceStream->size());
        });
        connect(
            d->sourceStream.get(),
            &PipeWireSourceStream::frameReceived,
            this,
            [this](const PipeWireFrame &frame) {
                if (frame.cursor) {
                    Q_EMIT cursorChanged(*frame.cursor);
                }
            },
            Qt::QueuedConnection);

        if (d->nodeId != 0 && d->pipeWireFd) {
            if (!d->sourceStream->createStream(d->nodeId, d->pipeWireFd)) {
                qCWarning(KRDP) << "Could not create PipeWire source stream" << d->sourceStream->error();
                d->session->close(RdpConnection::CloseReason::VideoInitFailed);
                return;
            }
            d->setSize(this, d->sourceStream->size());
        }
        d->sourceStream->setActive(d->streamingEnabled && d->nodeId != 0);
    }
}

void VideoStream::close()
{
    if (d->encodedStream) {
        d->encodedStream->stop();
    }
    if (d->sourceStream) {
        d->sourceStream->setActive(false);
    }
    if (d->frameSubmissionThread.joinable()) {
        d->frameSubmissionThread.request_stop();
        d->frameSubmissionThread.join();
    }

    {
        std::lock_guard lock(d->frameQueueMutex);
        d->frameQueue.clear();
    }

    d->pipeline->destroySurface(d->surface);
    d->activeEncodingMode.reset();
    Q_EMIT closed();
}

void VideoStream::queueFrame(const KRdp::VideoFrame &frame)
{
    if (d->session->state() != RdpConnection::State::Streaming || !d->pipeline->enabled()) {
        return;
    }

    std::lock_guard lock(d->frameQueueMutex);
    if (d->activeEncodingMode == RdpGfxPipeline::EncodingMode::H264) {
        if (frame.isKeyFrame) {
            d->frameQueue.clear();
        }
        d->frameQueue.append(frame);
        return;
    }

    if (d->activeEncodingMode == RdpGfxPipeline::EncodingMode::Progressive) {
        QRegion lastDamage;
        if (!d->frameQueue.isEmpty()) {
            lastDamage = d->frameQueue.last().damage;
            d->frameQueue.clear();
        }
        KRdp::VideoFrame nextFrame = frame;
        nextFrame.damage += lastDamage;
        d->frameQueue.append(std::move(nextFrame));
    }
}

void VideoStream::setStreamingEnabled(bool enabled)
{
    if (d->streamingEnabled == enabled) {
        return;
    }

    d->streamingEnabled = enabled;
    if (d->encodedStream) {
        if (enabled && d->nodeId != 0) {
            if (d->encodedStream->state() == PipeWireBaseEncodedStream::Idle) {
                d->encodedStream->start();
            }
        } else {
            d->encodedStream->stop();
        }
    }
    if (d->sourceStream) {
        d->sourceStream->setActive(enabled && d->nodeId != 0);
    }
}

void VideoStream::setVideoQuality(quint8 quality)
{
    d->quality = quality;
    if (d->encodedStream) {
        d->encodedStream->setQuality(quality);
    }
}

void VideoStream::setPipeWireSource(quint32 nodeId, int fd)
{
    d->nodeId = nodeId;
    d->pipeWireFd = fd;
    if (d->encodedStream) {
        d->encodedStream->setNodeId(nodeId);
        d->encodedStream->setFd(d->pipeWireFd);
        if (d->streamingEnabled) {
            d->encodedStream->start();
        }
    }
    if (d->sourceStream) {
        if (!d->sourceStream->createStream(nodeId, d->pipeWireFd)) {
            qCWarning(KRDP) << "Could not create PipeWire source stream" << d->sourceStream->error();
            d->session->close(RdpConnection::CloseReason::VideoInitFailed);
            return;
        }
        d->setSize(this, d->sourceStream->size());
        d->sourceStream->setActive(d->streamingEnabled);
    }
}

void VideoStream::onPacketReceived(const PipeWireEncodedStream::Packet &data)
{
    VideoFrame frameData;
    frameData.size = d->size;
    frameData.data = data.data();
    frameData.isKeyFrame = data.isKeyFrame();
    queueFrame(frameData);
}

void VideoStream::onFrameReceived(const PipeWireFrame &data)
{
    VideoFrame frameData;

    frameData.size = data.dataFrame ? data.dataFrame->size : QSize(data.dmabuf ? data.dmabuf->width : 0, data.dmabuf ? data.dmabuf->height : 0);
    frameData.damage = data.damage.value_or(QRegion(QRect(QPoint(0, 0), frameData.size)));
    if (data.presentationTimestamp) {
        frameData.presentationTimeStamp = clk::time_point(std::chrono::duration_cast<std::chrono::microseconds>(*data.presentationTimestamp));
    }

    if (data.dataFrame) {
        frameData.image = data.dataFrame->toImage().convertToFormat(QImage::Format_RGB32);
    } else if (data.dmabuf) {
        QImage image(frameData.size, QImage::Format_RGBA8888_Premultiplied);
        if (!d->dmaBufHandler.downloadFrame(image, data)) {
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

void VideoStream::clearSurface()
{
    d->pipeline->invalidateSurface(d->surface);
}

void VideoStream::sendFrame(const VideoFrame &frame)
{
    if (!d->loggedFirstFrame) {
        qCDebug(KRDP) << "Submitting first frame for geometry" << d->geometry << "frameSize" << frame.size << "damageEmpty" << frame.damage.isEmpty()
                      << "encodingMode" << (d->activeEncodingMode == RdpGfxPipeline::EncodingMode::H264 ? "h264" : "progressive");
        d->loggedFirstFrame = true;
    }
    if (!d->pipeline->ensureSurface(d->surface, frame.size, d->geometry.topLeft())) {
        return;
    }

    d->pipeline->submitFrame(d->surface, frame);
}

void VideoStream::updateRequestedFrameRate()
{
    auto rtt = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(d->session->networkDetection()->averageRTT()), std::chrono::milliseconds(1));
    auto now = clk::system_clock::now();

    FrameRateEstimate estimate;
    estimate.timeStamp = now;
    estimate.estimate = std::min(int(std::chrono::milliseconds(1000) / (rtt * std::max(d->pipeline->frameDelay(), 1))), d->maximumFrameRate);
    d->frameRateEstimates.append(estimate);

    if (now - d->lastFrameRateEstimation < FrameRateEstimateAveragePeriod) {
        return;
    }

    d->lastFrameRateEstimation = now;

    d->frameRateEstimates.erase(std::remove_if(d->frameRateEstimates.begin(),
                                               d->frameRateEstimates.end(),
                                               [now](const auto &estimate) {
                                                   return (estimate.timeStamp - now) > FrameRateEstimateAveragePeriod;
                                               }),
                                d->frameRateEstimates.cend());

    auto sum = std::accumulate(d->frameRateEstimates.cbegin(), d->frameRateEstimates.cend(), 0, [](int acc, const auto &estimate) {
        return acc + estimate.estimate;
    });
    auto average = sum / d->frameRateEstimates.size();

    constexpr qreal targetFrameRateSaturation = 0.5;
    auto frameRate = std::max(1.0, average * targetFrameRateSaturation);

    if (frameRate != d->requestedFrameRate) {
        d->requestedFrameRate = frameRate;
        if (d->encodedStream) {
            d->encodedStream->setMaxFramerate(frameRate, 1);
            d->encodedStream->setMaxPendingFrames(frameRate);
        }
        if (d->sourceStream) {
            d->sourceStream->setMaxFramerate(Fraction{static_cast<quint32>(frameRate), 1});
        }
    }
}

}

#include "moc_VideoStream.cpp"
