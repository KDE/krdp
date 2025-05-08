// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>

#include <QObject>
#include <QPoint>
#include <QRegion>
#include <QSize>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

/**
 * A frame of compressed video data.
 */
struct VideoFrame {
    /**
     * The size of the frame, in pixels.
     */
    QSize size;
    /**
     * h264 compressed data in YUV420 color space.
     */
    QByteArray data;
    /**
     * Area of the frame that was actually damaged.
     * TODO: Actually use this information.
     */
    QRegion damage;
    /**
     * Whether the packet contains all the information
     */
    bool isKeyFrame;
    /**
     * When was this frame presented.
     */
    std::chrono::system_clock::time_point presentationTimeStamp;
};

}
