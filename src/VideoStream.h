// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <optional>

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRegion>
#include <QSize>

#include <freerdp/server/rdpgfx.h>

#include "krdp_export.h"

namespace KRdp
{

class Session;

struct VideoFrame {
    QSize size;
    QByteArray data;

    QRegion damage;
};

class KRDP_EXPORT VideoStream : public QObject
{
    Q_OBJECT

public:
    VideoStream(Session *session);
    ~VideoStream();

    bool initialize();
    void close();

    void queueFrame(const VideoFrame &frame);

    void reset();

private:
    friend BOOL gfxChannelIdAssigned(RdpgfxServerContext *, uint32_t);
    friend uint32_t gfxCapsAdvertise(RdpgfxServerContext *, const RDPGFX_CAPS_ADVERTISE_PDU *);
    friend uint32_t gfxFrameAcknowledge(RdpgfxServerContext *, const RDPGFX_FRAME_ACKNOWLEDGE_PDU *);

    bool onChannelIdAssigned(uint32_t channelId);
    uint32_t onCapsAdvertise(const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise);
    uint32_t onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge);

    void performReset();
    void sendFrame(const VideoFrame &frame);

    class Private;
    const std::unique_ptr<Private> d;
};

}
