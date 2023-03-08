// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

#include <QDBusPendingCallWatcher>
#include <QObject>
#include <QPoint>
#include <QPointer>

#include <KPipeWire/PipeWireSourceStream>

#include "krdp_export.h"

namespace KRdp
{

struct VideoFrame;
class Server;

class KRDP_EXPORT PortalSession : public QObject
{
    Q_OBJECT

public:
    struct CursorImage {
        QPoint hotspot;
        QImage image;
    };

    PortalSession(Server *server);
    ~PortalSession();

    Q_SIGNAL void started();
    Q_SIGNAL void error();

    void sendEvent(QEvent *event);

    Q_SIGNAL void frameReceived(const VideoFrame &frame);
    Q_SIGNAL void cursorUpdate(const QPoint &position, std::optional<CursorImage> image);

private:
    void onCreateSession(uint code, const QVariantMap &result);
    void onDevicesSelected(uint code, const QVariantMap & /*result*/);
    void onSourcesSelected(uint code, const QVariantMap & /*result*/);
    void onSessionStarted(uint code, const QVariantMap &result);
    void onPacketReceived(const QByteArray &data);

    class Private;
    const std::unique_ptr<Private> d;
};

}
