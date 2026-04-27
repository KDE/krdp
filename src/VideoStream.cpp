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
#include <QSet>

#include <DmaBufHandler>
#include <PipeWireEncodedStream>
#include <freerdp/codec/progressive.h>
#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/update.h>

#include "NetworkDetection.h"
#include "PeerContext_p.h"
#include "RdpConnection.h"

#include "krdp_logging.h"

namespace KRdp
{

namespace clk = std::chrono;

// Maximum number of frames to contain in the queue.
constexpr clk::system_clock::duration FrameRateEstimateAveragePeriod = clk::seconds(1);
constexpr qsizetype MaximumInFlightFrames = 2;
constexpr uint32_t ProgressiveCodecContextId = 1;
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
    uint32_t codecContextId;
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
    using ProgressiveContextPtr = std::unique_ptr<PROGRESSIVE_CONTEXT, decltype(&progressive_context_free)>;

    RdpConnection *session;
    EncodingMode encodingMode = VideoStream::configuredEncodingMode();
    std::unique_ptr<PipeWireEncodedStream> encodedStream;
    std::unique_ptr<PipeWireSourceStream> sourceStream;
    DmaBufHandler dmaBufHandler;

    RdpGfxContextPtr gfxContext = RdpGfxContextPtr(nullptr, rdpgfx_server_context_free);
    ProgressiveContextPtr progressive = ProgressiveContextPtr(nullptr, progressive_context_free);

    uint32_t frameId = 0;
    uint32_t channelId = 0;
    quint32 nodeId = 0;

    uint16_t nextSurfaceId = 1;
    Surface surface;
    QSize size;

    bool pendingReset = true;
    bool enabled = false;
    bool streamingEnabled = false;
    bool capsConfirmed = false;
    bool channelOpen = false;
    bool avcSupported = false;
    bool yuv420Supported = false;
    bool progressiveSupported = false;

    std::jthread frameSubmissionThread;
    std::mutex frameQueueMutex;

    QQueue<VideoFrame> frameQueue;
    QSet<uint32_t> pendingFrames;

    std::mutex pendingFramesMutex;

    int maximumFrameRate = 120;
    std::atomic_int requestedFrameRate = 60;
    QQueue<FrameRateEstimate> frameRateEstimates;
    clk::system_clock::time_point lastFrameRateEstimation;

    std::atomic_int encodedFrames = 0;
    std::atomic_int frameDelay = 0;
    bool initialized = false;
    quint8 quality = 100;
};

static QString encodingModeName(VideoStream::EncodingMode mode)
{
    switch (mode) {
    case VideoStream::EncodingMode::H264:
        return QStringLiteral("h264");
    case VideoStream::EncodingMode::Progressive:
        return QStringLiteral("progressive");
    }
}

VideoStream::EncodingMode VideoStream::configuredEncodingMode()
{
    const QString mode = qEnvironmentVariable("KRDP_ENCODING_MODE", QStringLiteral("progressive")).trimmed().toLower();
    if (mode.isEmpty() || mode == QStringLiteral("progressive")) {
        return EncodingMode::Progressive;
    }
    if (mode == QStringLiteral("h264")) {
        return EncodingMode::H264;
    }

    qCWarning(KRDP) << "Unknown KRDP_ENCODING_MODE value" << mode << "- defaulting to h264";
    return EncodingMode::H264;
}

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

VideoStream::VideoStream(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
    if (d->encodingMode == EncodingMode::H264) {
        d->encodedStream = std::make_unique<PipeWireEncodedStream>();
        d->encodedStream->setEncodingPreference(PipeWireBaseEncodedStream::EncodingPreference::Speed);
        d->encodedStream->setColorRange(PipeWireBaseEncodedStream::ColorRange::Full);
        d->encodedStream->setEncoder(PipeWireEncodedStream::H264Baseline);
        d->encodedStream->setQuality(d->quality);
        d->encodedStream->setMaxFramerate(d->requestedFrameRate, 1);
        d->encodedStream->setMaxPendingFrames(d->requestedFrameRate);

        connect(d->encodedStream.get(), &PipeWireEncodedStream::newPacket, this, &VideoStream::onPacketReceived);
        connect(d->encodedStream.get(), &PipeWireEncodedStream::sizeChanged, this, [this](const QSize &size) {
            d->size = size;
        });
        connect(d->encodedStream.get(), &PipeWireEncodedStream::cursorChanged, this, &VideoStream::cursorChanged);
    } else {
        d->sourceStream = std::make_unique<PipeWireSourceStream>();
        d->sourceStream->setAllowDmaBuf(true);
        d->sourceStream->setDamageEnabled(true);
        d->sourceStream->setMaxFramerate({static_cast<quint32>(d->requestedFrameRate.load()), 1});
        connect(d->sourceStream.get(), &PipeWireSourceStream::frameReceived, this, &VideoStream::onFrameReceived, Qt::QueuedConnection);
        connect(d->sourceStream.get(), &PipeWireSourceStream::streamParametersChanged, this, [this]() {
            const QSize size = d->sourceStream->size();
            d->size = size;
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
    }
}

VideoStream::~VideoStream()
{
    close();
}

bool VideoStream::initialize()
{
    if (d->initialized) {
        return true;
    }

    d->gfxContext.reset(rdpgfx_server_context_new(contextForPeer(d->session->rdpPeer())->virtualChannelManager));
    if (!d->gfxContext) {
        qCWarning(KRDP) << "Failed to create graphics pipeline context";
        return false;
    }

    d->gfxContext->custom = this;
    d->gfxContext->ChannelIdAssigned = gfxChannelIdAssigned;
    d->gfxContext->CapsAdvertise = gfxCapsAdvertise;
    d->gfxContext->FrameAcknowledge = gfxFrameAcknowledge;
    d->gfxContext->QoeFrameAcknowledge = gfxQoEFrameAcknowledge;
    d->gfxContext->rdpcontext = d->session->rdpPeerContext();

    if (!d->gfxContext->Initialize(d->gfxContext.get(), FALSE)) {
        qCWarning(KRDP) << "Failed to initialize graphics pipeline context";
        d->gfxContext.reset();
        return false;
    }

    d->progressive.reset(progressive_context_new(TRUE));
    if (!d->progressive) {
        qCWarning(KRDP) << "Failed to create progressive codec context";
        d->gfxContext.reset();
        return false;
    }

    d->initialized = true;

    connect(d->session->networkDetection(), &NetworkDetection::rttChanged, this, &VideoStream::updateRequestedFrameRate);

    d->frameSubmissionThread = std::jthread([this](std::stop_token token) {
        while (!token.stop_requested()) {
            if (!hasInFlightCapacity() || !d->gfxContext || !d->capsConfirmed) {
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

    qCDebug(KRDP) << "Video stream initialized in" << encodingModeName(d->encodingMode) << "mode";

    return true;
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
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.clear();
    }
    {
        std::lock_guard lock(d->frameQueueMutex);
        d->frameQueue.clear();
    }

    destroySurface();

    if (d->gfxContext) {
        if (d->channelOpen) {
            d->gfxContext->Close(d->gfxContext.get());
            d->channelOpen = false;
        }
        d->gfxContext.reset();
    }
    d->initialized = false;

    Q_EMIT closed();
}

void VideoStream::queueFrame(const KRdp::VideoFrame &frame)
{
    if (d->session->state() != RdpConnection::State::Streaming || !d->enabled) {
        return;
    }

    std::lock_guard lock(d->frameQueueMutex);

    if (d->encodingMode == EncodingMode::H264) {
        d->frameQueue.append(frame);
        return;
    } else if (d->encodingMode == EncodingMode::Progressive) {
        // for the raster path we only need to keep the latest frame, but accumulate damage
        QRegion lastDamage;
        if (!d->frameQueue.isEmpty()) {
            lastDamage = d->frameQueue.last().damage;
            d->frameQueue.clear();
        }
        KRdp::VideoFrame nextFrame = frame;
        nextFrame.damage += lastDamage;
        d->frameQueue.append(std::move(frame));
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

void VideoStream::setStreamingEnabled(bool enabled)
{
    if (d->streamingEnabled == enabled) {
        return;
    }

    d->streamingEnabled = enabled;
    if (d->encodedStream) {
        if (enabled && d->nodeId != 0) {
            d->encodedStream->start();
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
    if (d->encodedStream) {
        d->encodedStream->setNodeId(nodeId);
        d->encodedStream->setFd(fd);
        if (d->streamingEnabled) {
            d->encodedStream->start();
        }
    }
    if (d->sourceStream) {
        d->sourceStream->setUsageHint(fd >= 0 ? PipeWireSourceStream::UsageHint::EncodeSoftware : PipeWireSourceStream::UsageHint::EncodeHardware);
        if (!d->sourceStream->createStream(nodeId, fd)) {
            qCWarning(KRDP) << "Could not create PipeWire source stream" << d->sourceStream->error();
            d->session->close(RdpConnection::CloseReason::VideoInitFailed);
            return;
        }
        d->size = d->sourceStream->size();
        d->sourceStream->setActive(d->streamingEnabled);
    }
}

bool VideoStream::onChannelIdAssigned(uint32_t channelId)
{
    d->channelId = channelId;

    return true;
}

uint32_t VideoStream::onCapsAdvertise(const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise)
{
    // Windows clients (mstsc) send CapsAdvertise twice: once during
    // initial setup and again after confirming. If we already confirmed
    // caps, this is a GFX channel reset — clear surface state so
    // surfaces get re-created on the next frame.
    if (d->capsConfirmed) {
        qCDebug(KRDP) << "GFX channel reset (re-advertisement), resetting surface state";
        d->capsConfirmed = false;
        d->pendingReset = true;
        destroySurface();
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.clear();
    }

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

        switch (caps.version) {
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

        qCDebug(KRDP) << " " << capVersionToString(caps.version) << "flags:" << Qt::hex << set.flags << Qt::dec << "AVC:" << caps.avcSupported
                      << "YUV420:" << caps.yuv420Supported;

        capsInformation.push_back(caps);
    }

    d->avcSupported = std::any_of(capsInformation.begin(), capsInformation.end(), [](const RdpCapsInformation &caps) {
        return caps.avcSupported;
    });
    d->yuv420Supported = std::any_of(capsInformation.begin(), capsInformation.end(), [](const RdpCapsInformation &caps) {
        return caps.yuv420Supported;
    });
    d->progressiveSupported = !capsInformation.empty();

    switch (d->encodingMode) {
    case EncodingMode::H264:
        if (!std::any_of(capsInformation.begin(), capsInformation.end(), [](const RdpCapsInformation &caps) {
                return caps.avcSupported && caps.yuv420Supported;
            })) {
            qCWarning(KRDP) << "Client does not support H.264 in YUV420 mode";
            d->session->close(RdpConnection::CloseReason::VideoInitFailed);
            return CHANNEL_RC_INITIALIZATION_ERROR;
        }
        break;
    case EncodingMode::Progressive:
        if (capsInformation.empty()) {
            qCWarning(KRDP) << "Client advertised no graphics capability sets";
            d->session->close(RdpConnection::CloseReason::VideoInitFailed);
            return CHANNEL_RC_INITIALIZATION_ERROR;
        }
        break;
    }

    auto maxVersion = std::max_element(capsInformation.begin(), capsInformation.end(), [](const auto &first, const auto &second) {
        return first.version < second.version;
    });

    qCDebug(KRDP) << "Selected caps:" << capVersionToString(maxVersion->version);

    RDPGFX_CAPS_CONFIRM_PDU capsConfirmPdu;
    capsConfirmPdu.capsSet = &(maxVersion->capSet);
    const UINT status = d->gfxContext->CapsConfirm(d->gfxContext.get(), &capsConfirmPdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "CapsConfirm failed" << status;
        return status;
    }

    d->capsConfirmed = true;

    return CHANNEL_RC_OK;
}

uint32_t VideoStream::onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge)
{
    auto id = frameAcknowledge->frameId;

    std::lock_guard lock(d->pendingFramesMutex);

    auto itr = d->pendingFrames.constFind(id);
    if (itr == d->pendingFrames.cend()) {
        qCWarning(KRDP) << "Got frame acknowledge for an unknown frame";
        return CHANNEL_RC_OK;
    }

    d->frameDelay = d->encodedFrames - frameAcknowledge->totalFramesDecoded;
    d->pendingFrames.erase(itr);

    return CHANNEL_RC_OK;
}

void VideoStream::onPacketReceived(const PipeWireEncodedStream::Packet &data)
{
    VideoFrame frameData;
    frameData.format = VideoFrame::Format::H264;
    frameData.size = d->size;
    frameData.data = data.data();
    frameData.isKeyFrame = data.isKeyFrame();
    queueFrame(frameData);
}

void VideoStream::onFrameReceived(const PipeWireFrame &data)
{
    VideoFrame frameData;

    frameData.format = VideoFrame::Format::Bgrx32;
    frameData.size = data.dataFrame ? data.dataFrame->size : QSize(data.dmabuf ? data.dmabuf->width : 0, data.dmabuf ? data.dmabuf->height : 0);
    frameData.damage = data.damage.value_or(QRegion(QRect(QPoint(0, 0), frameData.size)));
    if (data.presentationTimestamp) {
        frameData.presentationTimeStamp = clk::system_clock::time_point(*data.presentationTimestamp);
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

bool VideoStream::openChannel()
{
    if (!d->gfxContext) {
        return false;
    }
    if (d->channelOpen) {
        return true;
    }

    if (!d->gfxContext->Open(d->gfxContext.get())) {
        qCWarning(KRDP) << "Failed to open RDPGFX dynamic channel";
        return false;
    }

    d->channelOpen = true;
    return true;
}

void VideoStream::destroySurface()
{
    if (d->surface.id == 0) {
        return;
    }

    if (d->gfxContext && d->surface.codecContextId != 0) {
        RDPGFX_DELETE_ENCODING_CONTEXT_PDU deleteEncodingContextPdu = {};
        deleteEncodingContextPdu.surfaceId = d->surface.id;
        deleteEncodingContextPdu.codecContextId = d->surface.codecContextId;
        const UINT status = d->gfxContext->DeleteEncodingContext(d->gfxContext.get(), &deleteEncodingContextPdu);
        if (status != CHANNEL_RC_OK && status != CHANNEL_RC_NOT_INITIALIZED) {
            qCWarning(KRDP) << "DeleteEncodingContext failed" << status;
        }
    }

    if (d->gfxContext) {
        RDPGFX_DELETE_SURFACE_PDU deleteSurfacePdu = {};
        deleteSurfacePdu.surfaceId = d->surface.id;
        const UINT status = d->gfxContext->DeleteSurface(d->gfxContext.get(), &deleteSurfacePdu);
        if (status != CHANNEL_RC_OK && status != CHANNEL_RC_NOT_INITIALIZED) {
            qCWarning(KRDP) << "DeleteSurface failed" << status;
        }
    }

    if (d->progressive) {
        progressive_delete_surface_context(d->progressive.get(), d->surface.id);
    }

    d->surface = Surface{};
}

void VideoStream::performReset(QSize size)
{
    if (!d->gfxContext) {
        auto settings = d->session->rdpPeerContext()->settings;
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, size.width());
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, size.height());
        d->session->rdpPeerContext()->update->DesktopResize(d->session->rdpPeerContext());
        d->surface.size = size;
        return;
    }

    destroySurface();

    RDPGFX_RESET_GRAPHICS_PDU resetGraphicsPdu;
    resetGraphicsPdu.width = size.width();
    resetGraphicsPdu.height = size.height();
    resetGraphicsPdu.monitorCount = 1;

    MONITOR_DEF monitor = {};
    monitor.left = 0;
    monitor.right = size.width();
    monitor.top = 0;
    monitor.bottom = size.height();
    monitor.flags = MONITOR_PRIMARY;
    resetGraphicsPdu.monitorDefArray = &monitor;
    UINT status = d->gfxContext->ResetGraphics(d->gfxContext.get(), &resetGraphicsPdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "ResetGraphics failed" << status << "for size" << size;
        return;
    }

    RDPGFX_CREATE_SURFACE_PDU createSurfacePdu;
    createSurfacePdu.width = size.width();
    createSurfacePdu.height = size.height();
    uint16_t surfaceId = d->nextSurfaceId++;
    createSurfacePdu.surfaceId = surfaceId;
    createSurfacePdu.pixelFormat = GFX_PIXEL_FORMAT_XRGB_8888;
    status = d->gfxContext->CreateSurface(d->gfxContext.get(), &createSurfacePdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "CreateSurface failed" << status << "surface" << surfaceId << "size" << size;
        return;
    }

    d->surface = Surface{
        .id = surfaceId,
        .codecContextId = d->progressiveSupported ? ProgressiveCodecContextId : 0,
        .size = size,
    };

    if (d->progressiveSupported) {
        if (progressive_create_surface_context(d->progressive.get(), surfaceId, size.width(), size.height()) < 0) {
            qCWarning(KRDP) << "Failed to create progressive surface context";
            destroySurface();
            return;
        }
    }

    RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU mapSurfaceToOutputPdu;
    mapSurfaceToOutputPdu.outputOriginX = 0;
    mapSurfaceToOutputPdu.outputOriginY = 0;
    mapSurfaceToOutputPdu.surfaceId = surfaceId;
    status = d->gfxContext->MapSurfaceToOutput(d->gfxContext.get(), &mapSurfaceToOutputPdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "MapSurfaceToOutput failed" << status << "surface" << surfaceId;
        destroySurface();
        return;
    }
}

bool VideoStream::hasInFlightCapacity() const
{
    std::lock_guard lock(d->pendingFramesMutex);
    return d->pendingFrames.size() < MaximumInFlightFrames;
}

void VideoStream::sendFrame(const VideoFrame &frame)
{
    auto peer = d->session->rdpPeer();
    if (peer->IsWriteBlocked && peer->IsWriteBlocked(peer)) {
        return;
    }

    if (!d->gfxContext || !d->capsConfirmed) {
        return;
    }

    if (d->pendingReset) {
        d->pendingReset = false;
        performReset(frame.size);
    }
    if (d->surface.size != frame.size) {
        performReset(frame.size);
    }

    switch (frame.format) {
    case VideoFrame::Format::H264:
        sendFrameH264(frame);
        return;
    case VideoFrame::Format::Bgrx32:
        sendFrameProgressive(frame);
        return;
    }
}

void VideoStream::sendFrameH264(const VideoFrame &frame)
{
    if (!d->avcSupported || !d->yuv420Supported) {
        qCDebug(KRDP) << "Skipping H264 frame, client does not advertise AVC420 support";
        return;
    }

    if (frame.data.isEmpty()) {
        return;
    }

    if (d->surface.id == 0) {
        qCWarning(KRDP) << "No graphics surface available for H264 frame submission";
        return;
    }

    d->session->networkDetection()->startBandwidthMeasure();

    auto frameId = d->frameId++;

    d->encodedFrames++;

    {
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.insert(frameId);
    }

    RDPGFX_START_FRAME_PDU startFramePdu;
    RDPGFX_END_FRAME_PDU endFramePdu;

    auto now = QDateTime::currentDateTimeUtc().time();
    startFramePdu.timestamp = now.hour() << 22 | now.minute() << 16 | now.second() << 10 | now.msec();

    startFramePdu.frameId = frameId;
    endFramePdu.frameId = frameId;

    RDPGFX_SURFACE_COMMAND surfaceCommand;
    surfaceCommand.surfaceId = d->surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_AVC420;
    surfaceCommand.contextId = 0;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;
    surfaceCommand.left = 0;
    surfaceCommand.top = 0;
    surfaceCommand.right = frame.size.width();
    surfaceCommand.bottom = frame.size.height();
    surfaceCommand.width = frame.size.width();
    surfaceCommand.height = frame.size.height();
    surfaceCommand.length = 0;
    surfaceCommand.data = nullptr;

    RDPGFX_AVC420_BITMAP_STREAM avcStream;
    surfaceCommand.extra = &avcStream;

    avcStream.data = reinterpret_cast<BYTE *>(const_cast<char *>(frame.data.constData()));
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

    const UINT startStatus = d->gfxContext->StartFrame(d->gfxContext.get(), &startFramePdu);
    if (startStatus != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "StartFrame failed" << startStatus << "frameId" << frameId;
        d->session->networkDetection()->stopBandwidthMeasure();
        return;
    }

    const UINT commandStatus = d->gfxContext->SurfaceCommand(d->gfxContext.get(), &surfaceCommand);
    if (commandStatus != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "SurfaceCommand failed" << commandStatus << "frameId" << frameId << "surface" << d->surface.id << "encodedBytes"
                        << frame.data.size();
    }

    const UINT endStatus = d->gfxContext->EndFrame(d->gfxContext.get(), &endFramePdu);
    if (endStatus != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "EndFrame failed" << endStatus << "frameId" << frameId;
    }

    d->session->networkDetection()->stopBandwidthMeasure();
}

void VideoStream::sendFrameProgressive(const VideoFrame &frame)
{
    if (!d->progressiveSupported || !d->progressive) {
        qCDebug(KRDP) << "Skipping raster frame, progressive codec path is unavailable";
        return;
    }

    if (frame.image.isNull()) {
        return;
    }

    if (d->surface.id == 0) {
        qCWarning(KRDP) << "No graphics surface available for progressive frame submission";
        return;
    }

    QImage image = frame.image.convertToFormat(QImage::Format_RGB32);
    const QRect frameRect(QPoint(0, 0), image.size());
    auto invalidRegion = toRegion16(frame.damage, frameRect);
    if (!invalidRegion) {
        qCWarning(KRDP) << "Failed to build invalid region for progressive frame";
        return;
    }

    BYTE *encodedData = nullptr;
    UINT32 encodedSize = 0;
    const UINT32 rectCount = region16_n_rects(&*invalidRegion);
    const int compressionStatus = progressive_compress(d->progressive.get(),
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
        return;
    }

    d->session->networkDetection()->startBandwidthMeasure();

    auto frameId = d->frameId++;

    d->encodedFrames++;

    {
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.insert(frameId);
    }

    RDPGFX_START_FRAME_PDU startFramePdu;
    RDPGFX_END_FRAME_PDU endFramePdu;

    auto now = QDateTime::currentDateTimeUtc().time();
    startFramePdu.timestamp = now.hour() << 22 | now.minute() << 16 | now.second() << 10 | now.msec();

    startFramePdu.frameId = frameId;
    endFramePdu.frameId = frameId;

    const RECTANGLE_16 *extents = region16_extents(&*invalidRegion);

    RDPGFX_SURFACE_COMMAND surfaceCommand;
    surfaceCommand.surfaceId = d->surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_CAPROGRESSIVE;
    surfaceCommand.contextId = d->surface.codecContextId;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;
    surfaceCommand.left = extents->left;
    surfaceCommand.top = extents->top;
    surfaceCommand.right = extents->right;
    surfaceCommand.bottom = extents->bottom;
    surfaceCommand.width = frame.size.width();
    surfaceCommand.height = frame.size.height();
    surfaceCommand.length = encodedSize;
    surfaceCommand.data = encodedData;
    surfaceCommand.extra = nullptr;

    const UINT status = d->gfxContext->SurfaceFrameCommand(d->gfxContext.get(), &surfaceCommand, &startFramePdu, &endFramePdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "SurfaceFrameCommand failed" << status << "frameId" << frameId << "surface" << d->surface.id << "encodedBytes" << encodedSize
                        << "damageRects" << rectCount;
    }

    d->session->networkDetection()->stopBandwidthMeasure();
    region16_uninit(&*invalidRegion);
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
        if (d->encodedStream) {
            d->encodedStream->setMaxFramerate(frameRate, 1);
            d->encodedStream->setMaxPendingFrames(frameRate);
        }
        if (d->sourceStream) {
            d->sourceStream->setMaxFramerate({static_cast<quint32>(frameRate), 1});
        }
    }
}
}

#include "moc_VideoStream.cpp"
