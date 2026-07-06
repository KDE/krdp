// SPDX-FileCopyrightText: 2026 OpenAI
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "RdpGfxPipeline.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include <QDateTime>
#include <QMetaObject>
#include <QRegion>

#include <freerdp/peer.h>
#include <freerdp/channels/rdpgfx.h>
#include <freerdp/codec/progressive.h>

#include "NetworkDetection.h"
#include "PeerContext_p.h"
#include "RdpConnection.h"
#include "krdp_logging.h"

namespace KRdp
{

namespace
{
constexpr uint16_t ProgressiveCodecContextId = 1;
constexpr int MaximumInFlightFrames = 3;

struct RdpCapsInformation {
    RDPGFX_CAPSET capSet;
    uint32_t version = 0;
    bool avcSupported = false;
    bool yuv420Supported = false;
};

QString capVersionToString(uint32_t version)
{
    switch (version) {
    case RDPGFX_CAPVERSION_8:
        return QStringLiteral("8.0");
    case RDPGFX_CAPVERSION_81:
        return QStringLiteral("8.1");
    case RDPGFX_CAPVERSION_10:
        return QStringLiteral("10.0");
    case RDPGFX_CAPVERSION_101:
        return QStringLiteral("10.1");
    case RDPGFX_CAPVERSION_102:
        return QStringLiteral("10.2");
    case RDPGFX_CAPVERSION_103:
        return QStringLiteral("10.3");
    case RDPGFX_CAPVERSION_104:
        return QStringLiteral("10.4");
    case RDPGFX_CAPVERSION_105:
        return QStringLiteral("10.5");
    case RDPGFX_CAPVERSION_106:
        return QStringLiteral("10.6");
    case RDPGFX_CAPVERSION_107:
        return QStringLiteral("10.7");
    default:
        return QStringLiteral("unknown");
    }
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
}

BOOL gfxChannelIdAssigned(RdpgfxServerContext *context, uint32_t channelId)
{
    auto pipeline = reinterpret_cast<RdpGfxPipeline *>(context->custom);
    return pipeline->onChannelIdAssigned(channelId) ? TRUE : FALSE;
}

uint32_t gfxCapsAdvertise(RdpgfxServerContext *context, const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise)
{
    auto pipeline = reinterpret_cast<RdpGfxPipeline *>(context->custom);
    return pipeline->onCapsAdvertise(capsAdvertise);
}

uint32_t gfxFrameAcknowledge(RdpgfxServerContext *context, const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge)
{
    auto pipeline = reinterpret_cast<RdpGfxPipeline *>(context->custom);
    return pipeline->onFrameAcknowledge(frameAcknowledge);
}

uint32_t gfxQoeFrameAcknowledge(RdpgfxServerContext *, const RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU *)
{
    return CHANNEL_RC_OK;
}

class KRDP_NO_EXPORT RdpGfxPipeline::Private
{
public:
    using RdpGfxContextPtr = std::unique_ptr<RdpgfxServerContext, decltype(&rdpgfx_server_context_free)>;
    using ProgressiveContextPtr = std::unique_ptr<PROGRESSIVE_CONTEXT, decltype(&progressive_context_free)>;

    explicit Private(RdpConnection *session)
        : session(session)
    {
    }

    RdpConnection *session = nullptr;
    RdpGfxContextPtr gfxContext = RdpGfxContextPtr(nullptr, rdpgfx_server_context_free);
    ProgressiveContextPtr progressive = ProgressiveContextPtr(nullptr, progressive_context_free);
    QList<Output> outputs;
    QSet<uint16_t> knownSurfaces;
    std::optional<EncodingMode> activeEncodingMode;
    uint32_t frameId = 0;
    uint32_t channelId = 0;
    uint16_t nextSurfaceId = 1;
    bool initialized = false;
    bool enabled = false;
    bool capsConfirmed = false;
    bool channelOpen = false;
    bool outputsDirty = true;
    std::atomic_int encodedFrames = 0;
    std::atomic_int frameDelay = 0;
    QSet<uint32_t> pendingFrames;
    std::mutex pendingFramesMutex;
};

RdpGfxPipeline::RdpGfxPipeline(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>(session))
{
}

RdpGfxPipeline::~RdpGfxPipeline()
{
    close();
}

bool RdpGfxPipeline::h264Disabled()
{
    static const bool disabled = qEnvironmentVariableIntValue("KRDP_DISABLE_H264") != 0;
    return disabled;
}

bool RdpGfxPipeline::initialize()
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
    d->gfxContext->QoeFrameAcknowledge = gfxQoeFrameAcknowledge;
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
    return true;
}

bool RdpGfxPipeline::openChannel()
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

void RdpGfxPipeline::close()
{
    resetSurfaceState();
    {
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.clear();
    }

    if (d->gfxContext) {
        if (d->channelOpen) {
            d->gfxContext->Close(d->gfxContext.get());
            d->channelOpen = false;
        }
        d->gfxContext.reset();
    }

    d->activeEncodingMode.reset();
    d->capsConfirmed = false;
    d->initialized = false;
    d->outputsDirty = true;
}

bool RdpGfxPipeline::enabled() const
{
    return d->enabled;
}

void RdpGfxPipeline::setEnabled(bool enabled)
{
    if (d->enabled == enabled) {
        return;
    }

    d->enabled = enabled;
    Q_EMIT enabledChanged();
}

std::optional<RdpGfxPipeline::EncodingMode> RdpGfxPipeline::encodingMode() const
{
    return d->activeEncodingMode;
}

void RdpGfxPipeline::setOutputs(const QList<Output> &outputs)
{
    d->outputs = outputs;
    d->outputsDirty = true;
    Q_EMIT surfacesInvalidated();
}

void RdpGfxPipeline::invalidateSurface(Surface &surface)
{
    surface = Surface{};
}

void RdpGfxPipeline::destroySurface(Surface &surface)
{
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

    d->knownSurfaces.remove(surface.id);
    surface = Surface{};
}

bool RdpGfxPipeline::resetGraphics()
{
    if (!d->gfxContext || d->outputs.isEmpty()) {
        return false;
    }

    QRect desktopGeometry;
    for (const auto &output : d->outputs) {
        desktopGeometry |= output.geometry;
    }

    RDPGFX_RESET_GRAPHICS_PDU resetGraphicsPdu = {};
    resetGraphicsPdu.width = desktopGeometry.width();
    resetGraphicsPdu.height = desktopGeometry.height();
    resetGraphicsPdu.monitorCount = d->outputs.size();

    std::vector<MONITOR_DEF> monitors(d->outputs.size());
    for (int i = 0; i < d->outputs.size(); ++i) {
        const auto &geometry = d->outputs.at(i).geometry;
        auto &monitor = monitors[i];
        monitor.left = geometry.left();
        monitor.right = geometry.left() + geometry.width();
        monitor.top = geometry.top();
        monitor.bottom = geometry.top() + geometry.height();
        monitor.flags = d->outputs.at(i).primary ? MONITOR_PRIMARY : 0;
    }
    resetGraphicsPdu.monitorDefArray = monitors.data();

    const UINT status = d->gfxContext->ResetGraphics(d->gfxContext.get(), &resetGraphicsPdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "ResetGraphics failed" << status << "for desktop geometry" << desktopGeometry;
        return false;
    }

    resetSurfaceState();
    d->outputsDirty = false;
    Q_EMIT surfacesInvalidated();
    return true;
}

bool RdpGfxPipeline::ensureSurface(Surface &surface, const QSize &size, const QPoint &origin)
{
    if (!d->gfxContext || !d->capsConfirmed) {
        return false;
    }

    if (d->outputsDirty && !resetGraphics()) {
        return false;
    }

    if (surface.id != 0 && surface.size == size && surface.origin == origin) {
        return true;
    }

    destroySurface(surface);

    RDPGFX_CREATE_SURFACE_PDU createSurfacePdu = {};
    createSurfacePdu.width = size.width();
    createSurfacePdu.height = size.height();
    createSurfacePdu.surfaceId = d->nextSurfaceId++;
    createSurfacePdu.pixelFormat = GFX_PIXEL_FORMAT_XRGB_8888;
    UINT status = d->gfxContext->CreateSurface(d->gfxContext.get(), &createSurfacePdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "CreateSurface failed" << status << "surface" << createSurfacePdu.surfaceId << "size" << size;
        return false;
    }

    surface.id = createSurfacePdu.surfaceId;
    surface.codecContextId = d->activeEncodingMode == EncodingMode::Progressive ? ProgressiveCodecContextId : 0;
    surface.size = size;
    surface.origin = origin;
    d->knownSurfaces.insert(surface.id);

    if (d->activeEncodingMode == EncodingMode::Progressive) {
        if (progressive_create_surface_context(d->progressive.get(), surface.id, size.width(), size.height()) < 0) {
            qCWarning(KRDP) << "Failed to create progressive surface context";
            destroySurface(surface);
            return false;
        }
    }

    RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU mapSurfaceToOutputPdu = {};
    mapSurfaceToOutputPdu.outputOriginX = origin.x();
    mapSurfaceToOutputPdu.outputOriginY = origin.y();
    mapSurfaceToOutputPdu.surfaceId = surface.id;
    status = d->gfxContext->MapSurfaceToOutput(d->gfxContext.get(), &mapSurfaceToOutputPdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "MapSurfaceToOutput failed" << status << "surface" << surface.id;
        destroySurface(surface);
        return false;
    }

    return true;
}

bool RdpGfxPipeline::hasInFlightCapacity() const
{
    std::lock_guard lock(d->pendingFramesMutex);
    return d->pendingFrames.size() < MaximumInFlightFrames;
}

void RdpGfxPipeline::submitFrame(const Surface &surface, const VideoFrame &frame)
{
    auto peer = d->session->rdpPeer();
    if (peer->IsWriteBlocked && peer->IsWriteBlocked(peer)) {
        return;
    }

    if (!d->gfxContext || !d->capsConfirmed || surface.id == 0) {
        return;
    }

    if (d->activeEncodingMode == EncodingMode::H264) {
        sendFrameH264(surface, frame);
    } else if (d->activeEncodingMode == EncodingMode::Progressive) {
        sendFrameProgressive(surface, frame);
    }
}

int RdpGfxPipeline::frameDelay() const
{
    return d->frameDelay.load();
}

bool RdpGfxPipeline::onChannelIdAssigned(uint32_t channelId)
{
    d->channelId = channelId;
    return true;
}

uint32_t RdpGfxPipeline::onCapsAdvertise(const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise)
{
    if (d->capsConfirmed) {
        qCDebug(KRDP) << "GFX channel reset (re-advertisement), resetting surface state";
        d->capsConfirmed = false;
        d->outputsDirty = true;
        resetSurfaceState();
        {
            std::lock_guard lock(d->pendingFramesMutex);
            d->pendingFrames.clear();
        }
        Q_EMIT surfacesInvalidated();
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

    const bool supportsProgressive = !capsInformation.empty();
    const bool supportsH264 = std::any_of(capsInformation.begin(), capsInformation.end(), [](const RdpCapsInformation &caps) {
        return caps.avcSupported && caps.yuv420Supported;
    });

    EncodingMode negotiatedMode = EncodingMode::Progressive;
    if (!h264Disabled() && supportsH264) {
        negotiatedMode = EncodingMode::H264;
    } else if (!supportsProgressive) {
        qCWarning(KRDP) << "Client advertised no usable graphics capability sets";
        d->session->close(RdpConnection::CloseReason::VideoInitFailed);
        return CHANNEL_RC_INITIALIZATION_ERROR;
    }

    auto previousMode = d->activeEncodingMode;
    d->activeEncodingMode = negotiatedMode;
    if (previousMode != negotiatedMode) {
        QMetaObject::invokeMethod(
            this,
            [this, negotiatedMode]() {
                Q_EMIT encodingModeChanged(negotiatedMode);
            },
            Qt::BlockingQueuedConnection);
    }
    qCDebug(KRDP) << "Selected encoding mode:" << (negotiatedMode == EncodingMode::H264 ? "h264" : "progressive");

    auto maxVersion = std::max_element(capsInformation.begin(), capsInformation.end(), [](const auto &first, const auto &second) {
        return first.version < second.version;
    });

    qCDebug(KRDP) << "Selected caps:" << capVersionToString(maxVersion->version);

    RDPGFX_CAPS_CONFIRM_PDU capsConfirmPdu = {};
    capsConfirmPdu.capsSet = &(maxVersion->capSet);
    const UINT status = d->gfxContext->CapsConfirm(d->gfxContext.get(), &capsConfirmPdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "CapsConfirm failed" << status;
        return status;
    }

    d->capsConfirmed = true;
    d->outputsDirty = true;
    Q_EMIT surfacesInvalidated();
    return CHANNEL_RC_OK;
}

uint32_t RdpGfxPipeline::onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge)
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

void RdpGfxPipeline::resetSurfaceState()
{
    if (d->progressive) {
        for (uint16_t surfaceId : std::as_const(d->knownSurfaces)) {
            progressive_delete_surface_context(d->progressive.get(), surfaceId);
        }
    }

    d->knownSurfaces.clear();
}

void RdpGfxPipeline::sendFrameH264(const Surface &surface, const VideoFrame &frame)
{
    if (frame.data.isEmpty()) {
        return;
    }

    d->session->networkDetection()->startBandwidthMeasure();

    auto frameId = d->frameId++;
    d->encodedFrames++;

    {
        std::lock_guard lock(d->pendingFramesMutex);
        d->pendingFrames.insert(frameId);
    }

    RDPGFX_START_FRAME_PDU startFramePdu = {};
    RDPGFX_END_FRAME_PDU endFramePdu = {};

    auto now = QDateTime::currentDateTimeUtc().time();
    startFramePdu.timestamp = now.hour() << 22 | now.minute() << 16 | now.second() << 10 | now.msec();
    startFramePdu.frameId = frameId;
    endFramePdu.frameId = frameId;

    RDPGFX_SURFACE_COMMAND surfaceCommand = {};
    surfaceCommand.surfaceId = surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_AVC420;
    surfaceCommand.contextId = 0;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;
    surfaceCommand.left = 0;
    surfaceCommand.top = 0;
    surfaceCommand.right = frame.size.width();
    surfaceCommand.bottom = frame.size.height();
    surfaceCommand.width = frame.size.width();
    surfaceCommand.height = frame.size.height();

    RDPGFX_AVC420_BITMAP_STREAM avcStream = {};
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
        qCWarning(KRDP) << "SurfaceCommand failed" << commandStatus << "frameId" << frameId << "surface" << surface.id << "encodedBytes"
                        << frame.data.size();
    }

    const UINT endStatus = d->gfxContext->EndFrame(d->gfxContext.get(), &endFramePdu);
    if (endStatus != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "EndFrame failed" << endStatus << "frameId" << frameId;
    }

    d->session->networkDetection()->stopBandwidthMeasure();
}

void RdpGfxPipeline::sendFrameProgressive(const Surface &surface, const VideoFrame &frame)
{
    if (frame.image.isNull()) {
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

    RDPGFX_START_FRAME_PDU startFramePdu = {};
    RDPGFX_END_FRAME_PDU endFramePdu = {};

    auto now = QDateTime::currentDateTimeUtc().time();
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

    const UINT status = d->gfxContext->SurfaceFrameCommand(d->gfxContext.get(), &surfaceCommand, &startFramePdu, &endFramePdu);
    if (status != CHANNEL_RC_OK) {
        qCWarning(KRDP) << "SurfaceFrameCommand failed" << status << "frameId" << frameId << "surface" << surface.id << "encodedBytes" << encodedSize
                        << "damageRects" << rectCount;
    }

    d->session->networkDetection()->stopBandwidthMeasure();
    region16_uninit(&*invalidRegion);
}

}

#include "moc_RdpGfxPipeline.cpp"
