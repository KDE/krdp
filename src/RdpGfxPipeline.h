// SPDX-FileCopyrightText: 2026 OpenAI
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <optional>

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QSize>

#include <freerdp/server/rdpgfx.h>

#include "VideoFrame.h"
#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

class KRDP_EXPORT RdpGfxPipeline : public QObject
{
    Q_OBJECT

public:
    enum class EncodingMode {
        H264,
        Progressive,
    };
    Q_ENUM(EncodingMode)

    struct Output {
        QRect geometry;
        bool primary = false;
    };

    struct Surface {
        uint16_t id = 0;
        uint32_t codecContextId = 0;
        QSize size;
        QPoint origin;
    };

    explicit RdpGfxPipeline(RdpConnection *session);
    ~RdpGfxPipeline() override;

    static bool h264Disabled();

    bool initialize();
    bool openChannel();
    void close();

    bool enabled() const;
    void setEnabled(bool enabled);

    std::optional<EncodingMode> encodingMode() const;

    void setOutputs(const QList<Output> &outputs);
    void invalidateSurface(Surface &surface);
    void destroySurface(Surface &surface);
    bool ensureSurface(Surface &surface, const QSize &size, const QPoint &origin);

    bool hasInFlightCapacity() const;
    void submitFrame(const Surface &surface, const VideoFrame &frame);
    int frameDelay() const;

Q_SIGNALS:
    void enabledChanged();
    void encodingModeChanged(KRdp::RdpGfxPipeline::EncodingMode mode);
    void surfacesInvalidated();

private:
    friend BOOL gfxChannelIdAssigned(RdpgfxServerContext *, uint32_t);
    friend uint32_t gfxCapsAdvertise(RdpgfxServerContext *, const RDPGFX_CAPS_ADVERTISE_PDU *);
    friend uint32_t gfxFrameAcknowledge(RdpgfxServerContext *, const RDPGFX_FRAME_ACKNOWLEDGE_PDU *);

    bool onChannelIdAssigned(uint32_t channelId);
    uint32_t onCapsAdvertise(const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise);
    uint32_t onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge);

    void resetSurfaceState();
    bool resetGraphics();
    void sendFrameH264(const Surface &surface, const VideoFrame &frame);
    void sendFrameProgressive(const Surface &surface, const VideoFrame &frame);

    class Private;
    const std::unique_ptr<Private> d;
};

}
