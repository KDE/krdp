// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// This file is roughly based on grd-rdp-graphics-pipeline.c from Gnome Remote
// Desktop which is:
//
// SPDX-FileCopyrightText: 2021 Pascal Nowack
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoStream.h"

#include <algorithm>
#include <condition_variable>

#include <QDateTime>
#include <QQueue>

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>

#include "NetworkDetection.h"
#include "PeerContext_p.h"
#include "RdpConnection.h"

#include "krdp_logging.h"

namespace KRdp
{

namespace clk = std::chrono;

// Maximum number of frames to contain in the queue.
constexpr clk::system_clock::duration FrameRateEstimateAveragePeriod = clk::seconds(1);

struct RdpCapsInformation {
    uint32_t version;
    RDPGFX_CAPSET capSet;
    bool avcSupported : 1 = false;
    bool yuv420Supported : 1 = false;
};

const char *capVersionToString(uint32_t version)
{
    switch (version) {
    case RDPGFX_CAPVERSION_107:
        return "RDPGFX_CAPVERSION_107";
    case RDPGFX_CAPVERSION_106:
        return "RDPGFX_CAPVERSION_106";
    case RDPGFX_CAPVERSION_105:
        return "RDPGFX_CAPVERSION_105";
    case RDPGFX_CAPVERSION_104:
        return "RDPGFX_CAPVERSION_104";
    case RDPGFX_CAPVERSION_103:
        return "RDPGFX_CAPVERSION_103";
    case RDPGFX_CAPVERSION_102:
        return "RDPGFX_CAPVERSION_102";
    case RDPGFX_CAPVERSION_101:
        return "RDPGFX_CAPVERSION_101";
    case RDPGFX_CAPVERSION_10:
        return "RDPGFX_CAPVERSION_10";
    case RDPGFX_CAPVERSION_81:
        return "RDPGFX_CAPVERSION_81";
    case RDPGFX_CAPVERSION_8:
        return "RDPGFX_CAPVERSION_8";
    default:
        return "UNKNOWN_VERSION";
    }
}

BOOL gfxChannelIdAssigned(RdpgfxServerContext *context, uint32_t channelId)
{
    auto stream = reinterpret_cast<VideoStream *>(context->custom);
    if (stream->onChannelIdAssigned(channelId)) {
        return TRUE;
    }
    return FALSE;
}

uint32_t gfxCapsAdvertise(RdpgfxServerContext *context, const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise)
{
    auto stream = reinterpret_cast<VideoStream *>(context->custom);
    return stream->onCapsAdvertise(capsAdvertise);
}

uint32_t gfxFrameAcknowledge(RdpgfxServerContext *context, const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge)
{
    auto stream = reinterpret_cast<VideoStream *>(context->custom);
    return stream->onFrameAcknowledge(frameAcknowledge);
}

uint32_t gfxQoEFrameAcknowledge(RdpgfxServerContext *, const RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU *)
{
    return CHANNEL_RC_OK;
}

struct Surface {
    uint16_t id;
    QSize size;
};

struct FrameRateEstimate {
    clk::system_clock::time_point timeStamp;
    int estimate = 0;
};

class KRDP_NO_EXPORT VideoStream::Private
{
public:
    using RdpGfxContextPtr = std::unique_ptr<RdpgfxServerContext, decltype(&rdpgfx_server_context_free)>;

    RdpConnection *session;

    RdpGfxContextPtr gfxContext = RdpGfxContextPtr(nullptr, rdpgfx_server_context_free);

    uint32_t frameId = 0;
    uint32_t channelId = 0;

    uint16_t nextSurfaceId = 1;
    Surface surface;

    bool pendingReset = true;
    bool enabled = false;
    bool capsConfirmed = false;

    std::jthread frameSubmissionThread;
    std::mutex frameQueueMutex;

    QQueue<VideoFrame> frameQueue;
    QSet<uint32_t> pendingFrames;

    int maximumFrameRate = 120;
    int requestedFrameRate = 60;
    QQueue<FrameRateEstimate> frameRateEstimates;
    clk::system_clock::time_point lastFrameRateEstimation;

    std::atomic_int encodedFrames = 0;
    std::atomic_int frameDelay = 0;
};

VideoStream::VideoStream(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
}

VideoStream::~VideoStream()
{
}

bool VideoStream::initialize()
{
    if (d->gfxContext) {
        return true;
    }

    auto peerContext = reinterpret_cast<PeerContext *>(d->session->rdpPeerContext());

    d->gfxContext = Private::RdpGfxContextPtr{rdpgfx_server_context_new(peerContext->virtualChannelManager), rdpgfx_server_context_free};
    if (!d->gfxContext) {
        qCWarning(KRDP) << "Failed creating RDPGFX context";
        return false;
    }

    d->gfxContext->ChannelIdAssigned = gfxChannelIdAssigned;
    d->gfxContext->CapsAdvertise = gfxCapsAdvertise;
    d->gfxContext->FrameAcknowledge = gfxFrameAcknowledge;
    d->gfxContext->QoeFrameAcknowledge = gfxQoEFrameAcknowledge;

    d->gfxContext->custom = this;
    d->gfxContext->rdpcontext = d->session->rdpPeerContext();

    if (!d->gfxContext->Open(d->gfxContext.get())) {
        qCWarning(KRDP) << "Could not open GFX context";
        return false;
    }

    connect(d->session->networkDetection(), &NetworkDetection::rttChanged, this, &VideoStream::updateRequestedFrameRate);

    d->frameSubmissionThread = std::jthread([this](std::stop_token token) {
        while (!token.stop_requested()) {
            VideoFrame nextFrame;
            {
                std::unique_lock lock(d->frameQueueMutex);
                if (!d->frameQueue.isEmpty()) {
                    nextFrame = d->frameQueue.takeFirst();
                }
            }
            if (nextFrame.size.isEmpty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000) / d->requestedFrameRate);
                continue;
            }
            sendFrame(nextFrame);
        }
    });

    qCDebug(KRDP) << "Video stream initialized";

    return true;
}

void VideoStream::close()
{
    if (!d->gfxContext) {
        return;
    }

    d->gfxContext->Close(d->gfxContext.get());

    if (d->frameSubmissionThread.joinable()) {
        d->frameSubmissionThread.request_stop();
        d->frameSubmissionThread.join();
    }

    Q_EMIT closed();
}

void VideoStream::queueFrame(const KRdp::VideoFrame &frame)
{
    if (d->session->state() != RdpConnection::State::Streaming || !d->enabled) {
        return;
    }

    std::lock_guard lock(d->frameQueueMutex);
    d->frameQueue.append(frame);
}

void VideoStream::reset()
{
    d->pendingReset = true;
}

bool VideoStream::enabled() const
{
    return d->enabled;
}

void VideoStream::setEnabled(bool enabled)
{
    if (d->enabled == enabled) {
        return;
    }

    d->enabled = enabled;
    Q_EMIT enabledChanged();
}

uint32_t VideoStream::requestedFrameRate() const
{
    return d->requestedFrameRate;
}

bool VideoStream::onChannelIdAssigned(uint32_t channelId)
{
    d->channelId = channelId;

    return true;
}

uint32_t VideoStream::onCapsAdvertise(const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise)
{
    auto capsSets = capsAdvertise->capsSets;
    auto count = capsAdvertise->capsSetCount;

    std::vector<RdpCapsInformation> capsInformation;
    capsInformation.reserve(count);

    qCDebug(KRDP) << "Received caps:";
    for (int i = 0; i < count; ++i) {
        auto set = capsSets[i];

        RdpCapsInformation caps;
        caps.version = set.version;
        caps.capSet = set;

        switch (set.version) {
        case RDPGFX_CAPVERSION_107:
        case RDPGFX_CAPVERSION_106:
        case RDPGFX_CAPVERSION_105:
        case RDPGFX_CAPVERSION_104:
            caps.yuv420Supported = true;
            Q_FALLTHROUGH();
        case RDPGFX_CAPVERSION_103:
        case RDPGFX_CAPVERSION_102:
        case RDPGFX_CAPVERSION_101:
        case RDPGFX_CAPVERSION_10:
            if (!(set.flags & RDPGFX_CAPS_FLAG_AVC_DISABLED)) {
                caps.avcSupported = true;
            }
            break;
        case RDPGFX_CAPVERSION_81:
            if (set.flags & RDPGFX_CAPS_FLAG_AVC420_ENABLED) {
                caps.avcSupported = true;
                caps.yuv420Supported = true;
            }
            break;
        case RDPGFX_CAPVERSION_8:
            break;
        }

        qCDebug(KRDP) << " " << capVersionToString(caps.version) << "AVC:" << caps.avcSupported << "YUV420:" << caps.yuv420Supported;

        capsInformation.push_back(caps);
    }

    auto supported = std::any_of(capsInformation.begin(), capsInformation.end(), [](const RdpCapsInformation &caps) {
        return caps.avcSupported && caps.yuv420Supported;
    });

    if (!supported) {
        qCWarning(KRDP) << "Client does not support H.264 in YUV420 mode!";
        d->session->close(RdpConnection::CloseReason::VideoInitFailed);
        return CHANNEL_RC_INITIALIZATION_ERROR;
    }

    auto maxVersion = std::max_element(capsInformation.begin(), capsInformation.end(), [](const auto &first, const auto &second) {
        return first.version < second.version;
    });

    qCDebug(KRDP) << "Selected caps:" << capVersionToString(maxVersion->version);

    RDPGFX_CAPS_CONFIRM_PDU capsConfirmPdu;
    capsConfirmPdu.capsSet = &(maxVersion->capSet);
    d->gfxContext->CapsConfirm(d->gfxContext.get(), &capsConfirmPdu);

    d->capsConfirmed = true;

    return CHANNEL_RC_OK;
}

uint32_t VideoStream::onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge)
{
    auto id = frameAcknowledge->frameId;

    auto itr = d->pendingFrames.constFind(id);
    if (itr == d->pendingFrames.cend()) {
        qCWarning(KRDP) << "Got frame acknowledge for an unknown frame";
        return CHANNEL_RC_OK;
    }

    if (frameAcknowledge->queueDepth & SUSPEND_FRAME_ACKNOWLEDGEMENT) {
        qDebug() << "suspend frame ack";
    }

    d->frameDelay = d->encodedFrames - frameAcknowledge->totalFramesDecoded;
    d->pendingFrames.erase(itr);

    return CHANNEL_RC_OK;
}

void VideoStream::performReset(QSize size)
{
    RDPGFX_RESET_GRAPHICS_PDU resetGraphicsPdu;
    resetGraphicsPdu.width = size.width();
    resetGraphicsPdu.height = size.height();
    resetGraphicsPdu.monitorCount = 1;

    auto monitors = new MONITOR_DEF[1];
    monitors[0].left = 0;
    monitors[0].right = size.width();
    monitors[0].top = 0;
    monitors[0].bottom = size.height();
    monitors[0].flags = MONITOR_PRIMARY;
    resetGraphicsPdu.monitorDefArray = monitors;
    d->gfxContext->ResetGraphics(d->gfxContext.get(), &resetGraphicsPdu);

    RDPGFX_CREATE_SURFACE_PDU createSurfacePdu;
    createSurfacePdu.width = size.width();
    createSurfacePdu.height = size.height();
    uint16_t surfaceId = d->nextSurfaceId++;
    createSurfacePdu.surfaceId = surfaceId;
    createSurfacePdu.pixelFormat = GFX_PIXEL_FORMAT_XRGB_8888;
    d->gfxContext->CreateSurface(d->gfxContext.get(), &createSurfacePdu);

    d->surface = Surface{
        .id = surfaceId,
        .size = size,
    };

    RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU mapSurfaceToOutputPdu;
    mapSurfaceToOutputPdu.outputOriginX = 0;
    mapSurfaceToOutputPdu.outputOriginY = 0;
    mapSurfaceToOutputPdu.surfaceId = surfaceId;
    d->gfxContext->MapSurfaceToOutput(d->gfxContext.get(), &mapSurfaceToOutputPdu);
}

void VideoStream::sendFrame(const VideoFrame &frame)
{
    if (!d->gfxContext || !d->capsConfirmed) {
        return;
    }

    if (frame.data.size() == 0) {
        return;
    }

    if (d->pendingReset) {
        d->pendingReset = false;
        performReset(frame.size);
    }

    d->session->networkDetection()->startBandwidthMeasure();

    auto frameId = d->frameId++;

    d->encodedFrames++;

    d->pendingFrames.insert(frameId);

    RDPGFX_START_FRAME_PDU startFramePdu;
    RDPGFX_END_FRAME_PDU endFramePdu;

    auto now = QDateTime::currentDateTimeUtc().time();
    startFramePdu.timestamp = now.hour() << 22 | now.minute() << 16 | now.second() << 10 | now.msec();

    startFramePdu.frameId = frameId;
    endFramePdu.frameId = frameId;

    RDPGFX_SURFACE_COMMAND surfaceCommand;
    surfaceCommand.surfaceId = d->surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_AVC420;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;

    surfaceCommand.left = 0;
    surfaceCommand.top = 0;
    surfaceCommand.right = frame.size.width();
    surfaceCommand.bottom = frame.size.height();
    surfaceCommand.length = 0;
    surfaceCommand.data = nullptr;

    RDPGFX_AVC420_BITMAP_STREAM avcStream;
    surfaceCommand.extra = &avcStream;

    avcStream.data = (BYTE *)frame.data.data();
    avcStream.length = frame.data.length();

    avcStream.meta.numRegionRects = 1;
    auto rects = std::make_unique<RECTANGLE_16[]>(1);
    rects[0].left = 0;
    rects[0].top = 0;
    rects[0].right = frame.size.width();
    rects[0].bottom = frame.size.height();
    avcStream.meta.regionRects = rects.get();
    auto qualities = std::make_unique<RDPGFX_H264_QUANT_QUALITY[]>(1);
    avcStream.meta.quantQualityVals = qualities.get();
    qualities[0].qp = 22;
    qualities[0].p = 0;
    qualities[0].qualityVal = 100;

    d->gfxContext->StartFrame(d->gfxContext.get(), &startFramePdu);
    d->gfxContext->SurfaceCommand(d->gfxContext.get(), &surfaceCommand);

    d->gfxContext->EndFrame(d->gfxContext.get(), &endFramePdu);

    d->session->networkDetection()->stopBandwidthMeasure();
}

void VideoStream::updateRequestedFrameRate()
{
    auto rtt = std::max(clk::duration_cast<clk::milliseconds>(d->session->networkDetection()->averageRTT()), clk::milliseconds(1));
    auto now = clk::system_clock::now();

    FrameRateEstimate estimate;
    estimate.timeStamp = now;
    estimate.estimate = std::min(int(clk::milliseconds(1000) / (rtt * std::max(d->frameDelay.load(), 1))), d->maximumFrameRate);
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

    // we want some headroom so we can always clear our current load
    // and handle any other latency
    constexpr qreal targetFrameRateSaturation = 0.5;
    auto frameRate = std::max(1.0, average * targetFrameRateSaturation);

    if (frameRate != d->requestedFrameRate) {
        d->requestedFrameRate = frameRate;
        Q_EMIT requestedFrameRateChanged();
    }
}
}

#include "moc_VideoStream.cpp"
