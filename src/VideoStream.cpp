// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// This file is roughly based on grd-rdp-graphics-pipeline.c from Gnome Remote
// Desktop which is:
//
// SPDX-FileCopyrightText: 2021 Pascal Nowack
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoStream.h"
#include "VideoStreamSurface.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include <QQueue>
#include <QSet>

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/update.h>
#include <qassert.h>

#include "NetworkDetection.h"
#include "PeerContext_p.h"
#include "RdpConnection.h"

#include "krdp_logging.h"

namespace KRdp
{

namespace clk = std::chrono;

constexpr qsizetype MaximumInFlightFrames = 2; // in-flight window floor
constexpr double InFlightGain = 1.0; // window spans this many round trips of frames
constexpr double LatencyBudgetSec = 1.0; // never buffer more than this many seconds of video
constexpr double MinimumWindowFrameRate = 5.0; // floor for producer-rate window sizing (avoids stop-and-wait)
constexpr double ProducerFpsEwmaAlpha = 0.25; // smoothing for the producer-rate estimate
constexpr double RttEwmaAlpha = 0.125; // second-stage smoothing of the (already windowed) averageRTT
constexpr double MinimumValidRttMs = 5.0; // ignore implausibly-low RTT samples
constexpr double MaximumValidRttMs = 60000.0; // ignore garbage RTT samples

constexpr qsizetype HighQueueCount = 3; // Level of frames in the pending-send queue that pauses frames pre-encoder
constexpr qsizetype LowQueueCount = 1; // Level of frames in the pending-send queue that resumes the encoder

constexpr uint32_t ProgressiveCodecContextId = 1;

constexpr clk::system_clock::duration QualityUpdateInterval = clk::milliseconds(1500);
constexpr int MinAdaptiveQuality = 10;
constexpr int QualityStepUp = 5;
constexpr int QualityStepDown = 10;

struct BitrateAnchor {
    double pixels;
    double kbit;
};

// "Quality 100" targets by resolution, no fps term - matches RustDesk's base_bitrate().
constexpr std::array<BitrateAnchor, 4> FullQualityBitrateAnchors = {{
    {921'600.0, 1500.0}, // 1280x720
    {2'073'600.0, 3110.0}, // 1920x1080
    {3'686'400.0, 4500.0}, // 2560x1440
    {8'294'400.0, 7500.0}, // 3840x2160
}};

static double fullQualityKbit(double pixels)
{
    const auto *nearest = std::min_element(FullQualityBitrateAnchors.begin(), FullQualityBitrateAnchors.end(), [pixels](const auto &a, const auto &b) {
        return std::abs(a.pixels - pixels) < std::abs(b.pixels - pixels);
    });
    return nearest->kbit * (pixels / nearest->pixels);
}

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

class KRDP_NO_EXPORT VideoStream::Private
{
public:
    using RdpGfxContextPtr = std::unique_ptr<RdpgfxServerContext, decltype(&rdpgfx_server_context_free)>;
    using ProgressiveContextPtr = std::unique_ptr<PROGRESSIVE_CONTEXT, decltype(&progressive_context_free)>;

    RdpConnection *session;
    std::optional<EncodingMode> activeEncodingMode;
    std::unique_ptr<VideoStreamSurface> surface;

    RdpGfxContextPtr gfxContext = RdpGfxContextPtr(nullptr, rdpgfx_server_context_free);
    ProgressiveContextPtr progressive = ProgressiveContextPtr(nullptr, progressive_context_free);

    uint32_t frameId = 0;
    uint32_t channelId = 0;
    uint16_t nextSurfaceId = 1;
    bool enabled = false;
    bool streamingEnabled = false;
    bool capsConfirmed = false;
    bool channelOpen = false;

    std::jthread frameSubmissionThread;
    std::mutex frameQueueMutex;
    QQueue<VideoFrame> frameQueue;
    QSet<uint32_t> pendingFrames;
    std::mutex pendingFramesMutex;

    std::atomic_int requestedFrameRate = 60;
    std::atomic<qsizetype> maxInFlight{MaximumInFlightFrames}; // recomputed from RTT on rttChanged
    // Producer-rate estimate for window sizing (see VideoStream::effectiveProducerFps()).
    std::atomic<uint64_t> producedFrames = 0; // frames entering krdp; written from frame callbacks
    uint64_t lastProducedFrames = 0; // touched only by updateInFlightWindow()
    clk::steady_clock::time_point lastProducerRateUpdate{}; // touched only by updateInFlightWindow()
    double smoothedProducerFps = MinimumWindowFrameRate; // touched only by updateInFlightWindow()
    // RTT smoothing for window sizing (see updateInFlightWindow()); touched only there.
    double smoothedRttMs = 0.0; // EWMA of valid averageRTT samples
    bool hasSmoothedRtt = false;
    double baseRttMs = 0.0; // session-minimum valid RTT (path BDP floor)
    bool hasBaseRtt = false;

    bool initialized = false;
    quint8 quality = 100;
    quint8 qualityCap = 100;
    bool adaptiveQuality = true;
    clk::system_clock::time_point lastQualityUpdate;
};

static QString encodingModeName(VideoStream::EncodingMode mode)
{
    switch (mode) {
    case VideoStream::EncodingMode::H264:
        return QStringLiteral("h264");
    case VideoStream::EncodingMode::Progressive:
        return QStringLiteral("progressive");
    }
    Q_UNREACHABLE();
}

bool VideoStream::h264Disabled()
{
    static const bool h264Disabled = qEnvironmentVariableIntValue("KRDP_DISABLE_H264") != 0;
    return h264Disabled;
}

VideoStream::VideoStream(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
    d->surface = std::make_unique<VideoStreamSurface>(this);
}

void VideoStream::setActiveEncodingMode(EncodingMode mode)
{
    if (d->activeEncodingMode == mode) {
        return;
    }

    {
        std::lock_guard lock(d->frameQueueMutex);
        d->frameQueue.clear();
    }
    d->surface->setActiveEncodingMode(mode, d->quality, d->requestedFrameRate.load());
    d->activeEncodingMode = mode;
}

void VideoStream::setSize(const QSize &newSize)
{
    if (d->surface->size == newSize) {
        return;
    }

    d->surface->size = newSize;
    Q_EMIT sizeChanged(newSize);
}

bool VideoStream::streamingEnabled() const
{
    return d->streamingEnabled;
}

void VideoStream::failVideoInitialization()
{
    d->session->close(RdpConnection::CloseReason::VideoInitFailed);
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

    connect(d->session->networkDetection(), &NetworkDetection::rttChanged, this, &VideoStream::updateInFlightWindow);
    connect(d->session->networkDetection(), &NetworkDetection::bandwidthChanged, this, &VideoStream::updateAdaptiveQuality);

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

    qCDebug(KRDP) << "Video stream initialized with H.264" << (h264Disabled() ? "disabled" : "enabled");

    return true;
}

void VideoStream::close()
{
    if (d->surface->encodedStream) {
        d->surface->encodedStream->stop();
    }
    if (d->surface->sourceStream) {
        d->surface->sourceStream->setActive(false);
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
    d->activeEncodingMode.reset();
    d->initialized = false;

    Q_EMIT closed();
}

void VideoStream::queueFrame(const KRdp::VideoFrame &frame)
{
    if (d->session->state() != RdpConnection::State::Streaming || !d->enabled) {
        return;
    }
    d->producedFrames.fetch_add(1, std::memory_order_relaxed);

    if (d->activeEncodingMode == EncodingMode::H264) {
        std::lock_guard lock(d->frameQueueMutex);
        if (frame.isKeyFrame) {
            d->frameQueue.clear();
        }
        d->frameQueue.append(frame);
    } else if (d->activeEncodingMode == EncodingMode::Progressive) {
        std::lock_guard lock(d->frameQueueMutex);
        // for the raster path we only need to keep the latest frame, but accumulate damage
        QRegion lastDamage;
        if (!d->frameQueue.isEmpty()) {
            lastDamage = d->frameQueue.last().damage;
            d->frameQueue.clear();
        }
        VideoFrame nextFrame = frame;
        nextFrame.damage += lastDamage;
        d->frameQueue.append(std::move(nextFrame));
    }
    updateBackpressure();
}

void VideoStream::reset()
{
    d->surface->pendingReset = true;
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
    d->surface->setStreamingEnabled(enabled);
}

void VideoStream::applyQuality()
{
    d->surface->setVideoQuality(d->quality);
}

void VideoStream::setVideoQuality(quint8 quality)
{
    d->qualityCap = quality;
    d->quality = d->adaptiveQuality ? std::min(d->quality, d->qualityCap) : d->qualityCap;
    applyQuality();
}

void VideoStream::setAdaptiveQuality(bool enabled)
{
    if (d->adaptiveQuality == enabled) {
        return;
    }
    d->adaptiveQuality = enabled;
    if (!enabled) {
        d->quality = d->qualityCap;
        applyQuality();
    }
}

void VideoStream::seedQuality(quint8 quality)
{
    if (!d->adaptiveQuality || d->activeEncodingMode != EncodingMode::H264) {
        return;
    }
    const quint8 hi = std::max<quint8>(d->qualityCap, quint8(MinAdaptiveQuality));
    d->quality = std::clamp<quint8>(quality, quint8(MinAdaptiveQuality), hi);
    applyQuality();
}

void VideoStream::setRequestedSize(const QSize &size)
{
    d->surface->setRequestedSize(size);
}

void VideoStream::setPipeWireSource(quint32 nodeId, quint64 objectSerial, int fd)
{
    d->surface->setPipeWireSource(nodeId, objectSerial, fd);
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
        d->surface->pendingReset = true;
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

    const bool supportsProgresive = !capsInformation.empty();

    const bool supportsH264 = std::any_of(capsInformation.begin(), capsInformation.end(), [](const RdpCapsInformation &caps) {
        return caps.avcSupported && caps.yuv420Supported;
    });

    EncodingMode negotiatedMode = EncodingMode::Progressive;
    if (!h264Disabled() && supportsH264) {
        negotiatedMode = EncodingMode::H264;
    } else if (!supportsProgresive) {
        qCWarning(KRDP) << "Client advertised no usable graphics capability sets";
        d->session->close(RdpConnection::CloseReason::VideoInitFailed);
        return CHANNEL_RC_INITIALIZATION_ERROR;
    }

    QMetaObject::invokeMethod(
        this,
        [this, negotiatedMode]() {
            setActiveEncodingMode(negotiatedMode);
        },
        Qt::BlockingQueuedConnection); // RDP callbacks are on the connection thread, VideoStream operates on the main thread
    qCDebug(KRDP) << "Selected encoding mode:" << encodingModeName(negotiatedMode);

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

    d->pendingFrames.erase(itr);

    return CHANNEL_RC_OK;
}

void VideoStream::onPacketReceived(const PipeWireEncodedStream::Packet &data)
{
    d->surface->onPacketReceived(data);
}

void VideoStream::onFrameReceived(const PipeWireFrame &data)
{
    d->surface->onFrameReceived(data);
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
    auto &surface = d->surface->surface;
    if (surface.id == 0) {
        return;
    }

    if (d->gfxContext && surface.codecContextId != 0) {
        RDPGFX_DELETE_ENCODING_CONTEXT_PDU deleteEncodingContextPdu = {};
        deleteEncodingContextPdu.surfaceId = surface.id;
        deleteEncodingContextPdu.codecContextId = surface.codecContextId;
        const UINT status = d->gfxContext->DeleteEncodingContext(d->gfxContext.get(), &deleteEncodingContextPdu);
        if (status != CHANNEL_RC_OK && status != CHANNEL_RC_NOT_INITIALIZED) {
            qCWarning(KRDP) << "DeleteEncodingContext failed" << status;
        }
    }

    if (d->gfxContext) {
        RDPGFX_DELETE_SURFACE_PDU deleteSurfacePdu = {};
        deleteSurfacePdu.surfaceId = surface.id;
        const UINT status = d->gfxContext->DeleteSurface(d->gfxContext.get(), &deleteSurfacePdu);
        if (status != CHANNEL_RC_OK && status != CHANNEL_RC_NOT_INITIALIZED) {
            qCWarning(KRDP) << "DeleteSurface failed" << status;
        }
    }

    if (d->progressive) {
        progressive_delete_surface_context(d->progressive.get(), surface.id);
    }

    surface = Surface{};
}

void VideoStream::performReset(QSize newSize)
{
    auto &surface = d->surface->surface;
    if (!d->gfxContext) {
        auto settings = d->session->rdpPeerContext()->settings;
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, newSize.width());
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, newSize.height());
        d->session->rdpPeerContext()->update->DesktopResize(d->session->rdpPeerContext());
        surface.size = newSize;
        return;
    }

    destroySurface();

    RDPGFX_RESET_GRAPHICS_PDU resetGraphicsPdu;
    resetGraphicsPdu.width = newSize.width();
    resetGraphicsPdu.height = newSize.height();
    resetGraphicsPdu.monitorCount = 1;

    MONITOR_DEF monitor = {};
    monitor.left = 0;
    monitor.right = newSize.width();
    monitor.top = 0;
    monitor.bottom = newSize.height();
    monitor.flags = MONITOR_PRIMARY;
    resetGraphicsPdu.monitorDefArray = &monitor;
    UINT status = d->gfxContext->ResetGraphics(d->gfxContext.get(), &resetGraphicsPdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "ResetGraphics failed" << status << "for size" << newSize;
        return;
    }

    RDPGFX_CREATE_SURFACE_PDU createSurfacePdu;
    createSurfacePdu.width = newSize.width();
    createSurfacePdu.height = newSize.height();
    const uint16_t surfaceId = d->nextSurfaceId++;
    createSurfacePdu.surfaceId = surfaceId;
    createSurfacePdu.pixelFormat = GFX_PIXEL_FORMAT_XRGB_8888;
    status = d->gfxContext->CreateSurface(d->gfxContext.get(), &createSurfacePdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "CreateSurface failed" << status << "surface" << surfaceId << "size" << newSize;
        return;
    }

    surface = Surface{
        .id = surfaceId,
        .codecContextId = d->activeEncodingMode == EncodingMode::Progressive ? ProgressiveCodecContextId : 0,
        .size = newSize,
    };

    if (d->activeEncodingMode == EncodingMode::Progressive) {
        if (progressive_create_surface_context(d->progressive.get(), surfaceId, newSize.width(), newSize.height()) < 0) {
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
    }
}

double VideoStream::effectiveProducerFps()
{
    // Producer rate = frames entering krdp from the source/encoder callbacks. It is measured
    // upstream of the send window, so sizing the window from it cannot feed back into itself
    // (unlike client-decoded FPS). Invariant: producedFrames is written from frame callbacks;
    // the smoothing state is touched only here (updateInFlightWindow() is the sole caller, one
    // rttChanged connection, so no lock is needed). All returns are clamped to a bootstrap/
    // stop-and-wait floor (MinimumWindowFrameRate), and to the requested rate when it is above
    // that floor (so a very low requested rate yields the floor, not less).
    const double requested = d->requestedFrameRate.load();
    const double ceiling = std::max(MinimumWindowFrameRate, requested);
    const auto clampFps = [&](double f) {
        return std::clamp(f, MinimumWindowFrameRate, ceiling);
    };

    const auto now = clk::steady_clock::now();
    const uint64_t total = d->producedFrames.load(std::memory_order_relaxed);
    if (d->lastProducerRateUpdate == clk::steady_clock::time_point{}) {
        d->lastProducerRateUpdate = now;
        d->lastProducedFrames = total;
        // Optimistic bootstrap: seed from the requested rate so the initial full-screen
        // burst gets a usable window immediately; the EWMA converges down to the measured
        // producer rate as frames arrive.
        d->smoothedProducerFps = clampFps(requested);
        return d->smoothedProducerFps;
    }
    const double elapsedSec = clk::duration<double>(now - d->lastProducerRateUpdate).count();
    const uint64_t delta = total - d->lastProducedFrames;
    d->lastProducerRateUpdate = now;
    d->lastProducedFrames = total;
    if (elapsedSec <= 0.0 || delta == 0) {
        return clampFps(d->smoothedProducerFps); // idle: hold last active rate, do not shrink
    }
    const double instantFps = double(delta) / elapsedSec;
    d->smoothedProducerFps = d->smoothedProducerFps * (1.0 - ProducerFpsEwmaAlpha) + instantFps * ProducerFpsEwmaAlpha;
    return clampFps(d->smoothedProducerFps);
}

void VideoStream::updateInFlightWindow()
{
    // Size the in-flight window from the bandwidth-delay product: how many produced frames fit
    // within the round-trip time. averageRTT() is already windowed; we smooth it a second time
    // (EWMA) so transient RTT spikes do not immediately inflate the submission window, and floor
    // the smoothed value at the session-minimum RTT so the window never drops below the path's
    // BDP (which would collapse high-RTT throughput). Producer FPS is measured from frames
    // entering krdp; averageRTT() is published via atomics; both are safe to read here. The RTT
    // smoothing state is updated only from here, which the rttChanged connection invokes serially,
    // so no lock is needed. Falls back to the fixed floor until a valid RTT is known.
    const double fps = effectiveProducerFps();
    const double rttMs = clk::duration<double, std::milli>(d->session->networkDetection()->averageRTT()).count();
    qsizetype window = MaximumInFlightFrames;
    if (rttMs >= MinimumValidRttMs && rttMs < MaximumValidRttMs) {
        if (!d->hasBaseRtt || rttMs < d->baseRttMs) {
            d->baseRttMs = rttMs;
            d->hasBaseRtt = true;
        }
        if (!d->hasSmoothedRtt) {
            d->smoothedRttMs = rttMs;
            d->hasSmoothedRtt = true;
        } else {
            d->smoothedRttMs = (1.0 - RttEwmaAlpha) * d->smoothedRttMs + RttEwmaAlpha * rttMs;
        }
        const double effectiveRttMs = std::max(d->baseRttMs, d->smoothedRttMs);
        const double rttSec = effectiveRttMs / 1000.0;
        const qsizetype bdp = qsizetype(std::ceil(fps * rttSec * InFlightGain));
        const qsizetype cap = std::max<qsizetype>(MaximumInFlightFrames, qsizetype(std::ceil(fps * LatencyBudgetSec)));
        window = std::clamp(bdp, qsizetype(MaximumInFlightFrames), cap);
    }
    d->maxInFlight.store(window);
}

void VideoStream::updateAdaptiveQuality()
{
    if (!d->adaptiveQuality || d->activeEncodingMode != EncodingMode::H264) {
        return;
    }

    const quint32 goodputKbit = d->session->networkDetection()->bandwidth();
    if (goodputKbit == 0) {
        return;
    }

    const auto now = clk::system_clock::now();
    if (now - d->lastQualityUpdate < QualityUpdateInterval) {
        return;
    }

    const int fps = std::max(1, d->requestedFrameRate.load());
    const double pixels = double(d->surface->size.width()) * double(d->surface->size.height());
    if (pixels <= 0.0) {
        return;
    }
    const double fullKbit = fullQualityKbit(pixels);

    const int hi = std::max(int(d->qualityCap), MinAdaptiveQuality);
    int target = std::clamp(int(std::lround(goodputKbit / fullKbit * 100.0)), MinAdaptiveQuality, hi);

    const auto avg = clk::duration_cast<clk::milliseconds>(d->session->networkDetection()->averageRTT());
    const auto min = clk::duration_cast<clk::milliseconds>(d->session->networkDetection()->minimumRTT());
    const bool congested = min.count() > 0 && avg.count() > min.count() * 3 / 2;
    if (congested) {
        target = std::clamp(int(d->quality) - QualityStepDown, MinAdaptiveQuality, target);
    }

    int next = d->quality;
    if (target < next) {
        next = std::max(target, next - QualityStepDown);
    } else if (target > next && !congested) {
        next = std::min(target, next + QualityStepUp);
    }

    if (next == int(d->quality)) {
        return;
    }

    d->lastQualityUpdate = now;
    d->quality = quint8(next);
    applyQuality();
    qCDebug(KRDP) << "Adaptive quality ->" << d->quality << "(target" << target << "cap" << d->qualityCap << "goodput" << goodputKbit << "kbit/s, fps" << fps
                  << (congested ? ", congested" : "") << ")";
}

bool VideoStream::hasInFlightCapacity() const
{
    std::lock_guard lock(d->pendingFramesMutex);
    return d->pendingFrames.size() < d->maxInFlight.load();
}

void VideoStream::sendFrame(const VideoFrame &frame)
{
    auto peer = d->session->rdpPeer();
    if (peer->IsWriteBlocked && peer->IsWriteBlocked(peer)) {
        return;
    }

    if (!d->activeEncodingMode) {
        return;
    }

    if (!d->gfxContext || !d->capsConfirmed) {
        return;
    }

    if (d->surface->pendingReset) {
        d->surface->pendingReset = false;
        performReset(frame.size);
    }
    if (d->surface->surface.size != frame.size) {
        performReset(frame.size);
    }

    const auto frameId = d->frameId++;
    {
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.insert(frameId);
    }

    bool submitted = false;
    if (d->activeEncodingMode == EncodingMode::H264) {
        submitted = d->surface->sendFrameH264(d->gfxContext.get(), frameId, frame);
    } else {
        submitted = d->surface->sendFrameProgressive(d->gfxContext.get(), d->progressive.get(), frameId, frame);
    }

    if (!submitted) {
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.remove(frameId);
    }

    QMetaObject::invokeMethod(this, &VideoStream::updateBackpressure, Qt::QueuedConnection);
}

void VideoStream::updateBackpressure()
{
    if (!d->surface->encodedStream) {
        return;
    }

    qsizetype pendingSendQueueCount = 0;
    {
        std::lock_guard lock(d->frameQueueMutex);
        pendingSendQueueCount = d->frameQueue.count();
    }

    const bool encodingPaused = d->surface->encodedStream->encoderPaused();

    if (!encodingPaused && pendingSendQueueCount > HighQueueCount) {
        qCDebug(KRDP) << "Encoder backpressure activated" << pendingSendQueueCount;
        d->surface->encodedStream->setEncoderPaused(true);
    } else if (encodingPaused && pendingSendQueueCount < LowQueueCount) {
        qCDebug(KRDP) << "Encoder backpressure released" << pendingSendQueueCount;
        d->surface->encodedStream->setEncoderPaused(false);
    }
}
}

#include "moc_VideoStream.cpp"
