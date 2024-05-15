// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <QImage>
#include <QObject>
#include <QPoint>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

/**
 * Encapsulates cursor-specific parts of the RDP protocol.
 *
 * Cursor information is sent separately from the video stream. We need to keep
 * track of some state, most importantly which cursor images have already been
 * sent so we don't have to re-send those. This class takes care of doing that
 * and the actual sending.
 */
class KRDP_EXPORT Cursor : public QObject
{
    Q_OBJECT

public:
    struct CursorUpdate {
        QPoint hotspot;
        QImage image;

        uint32_t cacheId = 0;
        std::chrono::steady_clock::time_point lastUsed;

        bool operator==(const CursorUpdate &other) const;
    };

    enum class CursorType {
        Hidden,
        SystemDefault,
        Image,
    };

    Cursor(RdpConnection *session);
    ~Cursor();

    void update(const CursorUpdate &update);

private:
    void setCursorType(CursorType type);

    class Private;
    const std::unique_ptr<Private> d;
};

size_t qHash(const KRdp::Cursor::CursorUpdate &update, size_t seed = 0);

}
