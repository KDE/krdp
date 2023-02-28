// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "VideoStream.h"

#include <QDateTime>

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>

#include "PeerContext_p.h"
#include "Session.h"

#include "krdp_logging.h"

namespace KRdp
{

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

class KRDP_NO_EXPORT VideoStream::Private
{
public:
    Session *session;

    RdpgfxServerContext *gfxContext = nullptr;

    wStream *stream = nullptr;

    uint32_t frameId = 0;

    uint32_t channelId = 0;

    uint16_t nextSurfaceId = 1;
    Surface surface;

    bool pendingReset = true;
};

VideoStream::VideoStream(Session *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
}

VideoStream::~VideoStream()
{
    if (d->stream) {
        Stream_Free(d->stream, TRUE);
        d->stream = nullptr;
    }

    // if (d->gfxContext) {
    //     d->gfxContext->Close(d->gfxContext);
    // }
}

bool VideoStream::initialize()
{
    if (d->gfxContext) {
        return true;
    }

    auto peerContext = reinterpret_cast<PeerContext *>(d->session->rdpPeer()->context);

    d->gfxContext = rdpgfx_server_context_new(peerContext->virtualChannelManager);
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

    if (!d->gfxContext->Open(d->gfxContext)) {
        qCWarning(KRDP) << "Could not open GFX context";
        return false;
    }

    qCDebug(KRDP) << "Video stream initialized";

    return true;
}

void VideoStream::sendFrame(const VideoFrame &frame)
{
    if (!d->gfxContext) {
        return;
    }

    if (d->pendingReset) {
        d->pendingReset = false;
        performReset();
    }

    // auto alignedSize = QSize{
    //     frame.size.width() + (frame.size.width() % 16 > 0 ? 16 - frame.size.width() : 0),
    //     frame.size.height() + (frame.size.height() % 16 > 0 ? 16 - frame.size.height() : 0)
    // };

    RDPGFX_START_FRAME_PDU startFramePdu;
    RDPGFX_END_FRAME_PDU endFramePdu;

    startFramePdu.timestamp = QDateTime::currentMSecsSinceEpoch();
    startFramePdu.frameId = d->frameId++;
    endFramePdu.frameId = startFramePdu.frameId;

    RDPGFX_SURFACE_COMMAND surfaceCommand;
    surfaceCommand.surfaceId = d->surface.id;
    surfaceCommand.codecId = RDPGFX_CODECID_AVC420;
    surfaceCommand.format = PIXEL_FORMAT_BGRX32;

    // auto damageRect = frame.damage.boundingRect();

    surfaceCommand.left = 0;
    surfaceCommand.top = 0;
    // surfaceCommand.right = damageRect.x() + damageRect.width();
    // surfaceCommand.bottom = damageRect.y() + damageRect.height();
    surfaceCommand.right = 1024;
    surfaceCommand.bottom = 768;
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
    rects[0].right = 1024;
    rects[0].bottom = 768;
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

    d->gfxContext->StartFrame(d->gfxContext, &startFramePdu);
    d->gfxContext->SurfaceCommand(d->gfxContext, &surfaceCommand);

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

    d->gfxContext->EndFrame(d->gfxContext, &endFramePdu);

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

void VideoStream::reset()
{
    d->pendingReset = true;
}

bool VideoStream::onChannelIdAssigned(uint32_t channelId)
{
    qDebug() << __PRETTY_FUNCTION__ << channelId;

    d->channelId = channelId;

    return true;
}

uint32_t VideoStream::onCapsAdvertise(const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise)
{
    return CHANNEL_RC_OK;
}

uint32_t VideoStream::onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge)
{
    return CHANNEL_RC_OK;
}

void VideoStream::performReset()
{
    RDPGFX_RESET_GRAPHICS_PDU resetGraphicsPdu;
    resetGraphicsPdu.width = 1024;
    resetGraphicsPdu.height = 768;
    resetGraphicsPdu.monitorCount = 1;

    auto monitors = new MONITOR_DEF[1];
    monitors[0].left = 0;
    monitors[0].right = 1024;
    monitors[0].top = 0;
    monitors[0].bottom = 768;
    monitors[0].flags = MONITOR_PRIMARY;
    resetGraphicsPdu.monitorDefArray = monitors;
    d->gfxContext->ResetGraphics(d->gfxContext, &resetGraphicsPdu);

    RDPGFX_CREATE_SURFACE_PDU createSurfacePdu;
    createSurfacePdu.width = 1024;
    createSurfacePdu.height = 768;
    uint16_t surfaceId = d->nextSurfaceId++;
    createSurfacePdu.surfaceId = surfaceId;
    createSurfacePdu.pixelFormat = GFX_PIXEL_FORMAT_XRGB_8888;
    d->gfxContext->CreateSurface(d->gfxContext, &createSurfacePdu);

    d->surface = Surface{
        .id = surfaceId,
        .size = QSize{1024, 768},
    };

    RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU mapSurfaceToOutputPdu;
    mapSurfaceToOutputPdu.outputOriginX = 0;
    mapSurfaceToOutputPdu.outputOriginY = 0;
    mapSurfaceToOutputPdu.surfaceId = surfaceId;
    d->gfxContext->MapSurfaceToOutput(d->gfxContext, &mapSurfaceToOutputPdu);
}

}
