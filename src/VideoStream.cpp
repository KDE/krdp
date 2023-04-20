// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "VideoStream.h"

#include <condition_variable>

#include <QDateTime>
#include <QQueue>

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>

#include "NetworkDetection.h"
#include "PeerContext_p.h"
#include "Session.h"

#include "krdp_logging.h"

namespace KRdp
{

namespace clk = std::chrono;

// Maximum number of frames to contain in the queue.
constexpr qsizetype MaxQueueSize = 10;

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

uint32_t gfxCacheImportOffer(RdpgfxServerContext *context, const RDPGFX_CACHE_IMPORT_OFFER_PDU *cacheImportOffer)
{
    RDPGFX_CACHE_IMPORT_REPLY_PDU cacheImportReply;
    return context->CacheImportReply(context, &cacheImportReply);
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

struct FrameInfo {
    clk::system_clock::time_point submitTimeStamp;
    clk::system_clock::duration frameTime;
};

class KRDP_NO_EXPORT VideoStream::Private
{
public:
    using RdpGfxContextPtr = std::unique_ptr<RdpgfxServerContext, decltype(&rdpgfx_server_context_free)>;

    Session *session;

    RdpGfxContextPtr gfxContext = RdpGfxContextPtr(nullptr, rdpgfx_server_context_free);

    uint32_t frameId = 0;
    uint32_t channelId = 0;

    uint16_t nextSurfaceId = 1;
    Surface surface;

    bool pendingReset = true;
    bool enabled = false;
    std::atomic_bool suspended = false;

    std::jthread frameSubmissionThread;
    std::mutex frameQueueMutex;

    QQueue<VideoFrame> frameQueue;
    QSet<uint32_t> pendingFrames;

    int maximumFrameRate = 60;
    int requestedFrameRate = 60;

    std::atomic_int encodedFrames = 0;
    uint32_t activateThrottlingThreshold = 4;
    std::atomic_int frameDelay = 0;
};

VideoStream::VideoStream(Session *session)
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

    auto peerContext = reinterpret_cast<PeerContext *>(d->session->rdpPeer()->context);

    d->gfxContext = Private::RdpGfxContextPtr{rdpgfx_server_context_new(peerContext->virtualChannelManager), rdpgfx_server_context_free};
    if (!d->gfxContext) {
        qCWarning(KRDP) << "Failed creating RDPGFX context";
        return false;
    }

    d->gfxContext->ChannelIdAssigned = gfxChannelIdAssigned;
    d->gfxContext->CapsAdvertise = gfxCapsAdvertise;
    d->gfxContext->CacheImportOffer = gfxCacheImportOffer;
    d->gfxContext->FrameAcknowledge = gfxFrameAcknowledge;
    d->gfxContext->QoeFrameAcknowledge = gfxQoEFrameAcknowledge;

    d->gfxContext->custom = this;
    d->gfxContext->rdpcontext = d->session->rdpPeer()->context;

    if (!d->gfxContext->Open(d->gfxContext.get())) {
        qCWarning(KRDP) << "Could not open GFX context";
        return false;
    }

    connect(d->session->networkDetection(), &NetworkDetection::rttChanged, this, &VideoStream::updateRequestedFrameRate);

    d->frameSubmissionThread = std::jthread([this](std::stop_token token) {
        while (!token.stop_requested()) {
            {
                std::unique_lock lock(d->frameQueueMutex);
                if (!d->frameQueue.isEmpty()) {
                    sendFrame(d->frameQueue.takeFirst());
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000) / d->requestedFrameRate);
        }
    });

    qCDebug(KRDP) << "Video stream initialized";

    return true;
}

void VideoStream::close()
{
    if (d->gfxContext) {
        d->gfxContext->Close(d->gfxContext.get());
    }

    if (d->frameSubmissionThread.joinable()) {
        d->frameSubmissionThread.request_stop();
        d->frameSubmissionThread.join();
    }
}

void VideoStream::queueFrame(const KRdp::VideoFrame &frame)
{
    if (d->session->state() != Session::State::Streaming || !d->enabled) {
        return;
    }

    std::lock_guard lock(d->frameQueueMutex);
    d->frameQueue.append(frame);

    while (d->frameQueue.size() > MaxQueueSize) {
        d->frameQueue.pop_front();
    }
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
    return CHANNEL_RC_OK;
}

uint32_t VideoStream::onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge)
{
    auto id = frameAcknowledge->frameId;

    auto itr = d->pendingFrames.find(id);
    if (itr == d->pendingFrames.end()) {
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

void VideoStream::performReset(const QSize &size)
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
    if (!d->gfxContext) {
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

    // auto alignedSize = QSize{
    //     frame.size.width() + (frame.size.width() % 16 > 0 ? 16 - frame.size.width() : 0),
    //     frame.size.height() + (frame.size.height() % 16 > 0 ? 16 - frame.size.height() : 0)
    // };
    auto frameId = d->frameId++;

    d->encodedFrames++;

    d->pendingFrames.insert(frameId);

    RDPGFX_START_FRAME_PDU startFramePdu;
    RDPGFX_END_FRAME_PDU endFramePdu;

    startFramePdu.timestamp = QDateTime::currentMSecsSinceEpoch();
    startFramePdu.frameId = frameId;
    endFramePdu.frameId = frameId;

    RDPGFX_SURFACE_COMMAND surfaceCommand;
    surfaceCommand.surfaceId = d->surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_AVC420;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;

    // auto damageRect = frame.damage.boundingRect();

    surfaceCommand.left = 0;
    surfaceCommand.top = 0;
    // surfaceCommand.right = damageRect.x() + damageRect.width();
    // surfaceCommand.bottom = damageRect.y() + damageRect.height();
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

    // for (int i = 0; i < frame.damage.rectCount(); ++i) {
    //     auto rect = *(frame.damage.begin() + i);
    //     rects[i].left = rect.x();
    //     rects[i].top = rect.y();
    //     rects[i].right = rect.x() + rect.width();
    //     rects[i].bottom = rect.y() + rect.height();
    //
    //     qualities[i].qp = 22;
    //     qualities[i].p = 0;
    //     qualities[i].qualityVal = 100;
    // }

    d->gfxContext->StartFrame(d->gfxContext.get(), &startFramePdu);
    d->gfxContext->SurfaceCommand(d->gfxContext.get(), &surfaceCommand);

    // RDPGFX_SURFACE_TO_SURFACE_PDU surfacePdu;
    // surfacePdu.surfaceIdSrc = d->surface.id;
    // surfacePdu.surfaceIdDest = d->surface.id;
    //
    // RDPGFX_POINT16 destinationPosition;
    //
    // for (int i = 0; i < frame.damage.rectCount(); ++i) {
    //     auto rect = *(frame.damage.begin() + i);
    //     destinationPosition.x = rect.x();
    //     destinationPosition.y = rect.y();
    //     surfacePdu.destPts = &destinationPosition;
    //     surfacePdu.destPtsCount = 1;
    //     surfacePdu.rectSrc = rects[i];
    //
    //     d->gfxContext->SurfaceToSurface(d->gfxContext, &surfacePdu);
    // }

    d->gfxContext->EndFrame(d->gfxContext.get(), &endFramePdu);

    d->session->networkDetection()->stopBandwidthMeasure();

    // rdpUpdate *update = d->session->rdpPeer()->context->update;
    //
    // const SURFACE_FRAME_MARKER beginMarker {
    //     .frameAction = SURFACECMD_FRAMEACTION_BEGIN,
    //     .frameId = d->frameId,
    // };
    // update->SurfaceFrameMarker(update->context, &beginMarker);
    //
    // SURFACE_BITS_COMMAND surfaceBits;
    //
    // update->SurfaceBits(update->context, &surfaceBits);
    //
    // const SURFACE_FRAME_MARKER endMarker {
    //     .frameAction = SURFACECMD_FRAMEACTION_END,
    //     .frameId = d->frameId,
    // };
    // update->SurfaceFrameMarker(update->context, &endMarker);
}

void VideoStream::updateRequestedFrameRate()
{
    auto rtt = clk::duration_cast<clk::milliseconds>(d->session->networkDetection()->averageRTT());
    auto estimatedFromRTT = clk::milliseconds(1000) / (rtt * std::max(d->frameDelay.load(), 1));
    d->requestedFrameRate = std::clamp(estimatedFromRTT, 1l, static_cast<clk::seconds::rep>(d->maximumFrameRate));
    d->suspended = false;
    Q_EMIT requestedFrameRateChanged();
}
}
