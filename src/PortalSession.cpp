// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PortalSession.h"

#include <QMouseEvent>
#include <QQueue>

#include <linux/input.h>

#include <KPipeWire/PipeWireRecord>

#include "PortalSession_p.h"
#include "VideoStream.h"
#include "krdp_logging.h"
#include "xdp_dbus_remotedesktop_interface.h"
#include "xdp_dbus_screencast_interface.h"

namespace KRdp
{

static const QString dbusService = QStringLiteral("org.freedesktop.portal.Desktop");
static const QString dbusPath = QStringLiteral("/org/freedesktop/portal/desktop");
static const QString dbusRequestInterface = QStringLiteral("org.freedesktop.portal.Request");
static const QString dbusResponse = QStringLiteral("Response");
static const QString dbusSessionInterface = QStringLiteral("org.freedesktop.portal.Session");

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalSessionStream &stream)
{
    arg.beginStructure();
    arg >> stream.nodeId;

    arg.beginMap();
    while (!arg.atEnd()) {
        QString key;
        QVariant map;
        arg.beginMapEntry();
        arg >> key >> map;
        arg.endMapEntry();
        stream.map.insert(key, map);
    }
    arg.endMap();
    arg.endStructure();

    return arg;
}

void PortalRequest::onStarted(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (!reply.isError()) {
        QDBusConnection::sessionBus().connect(QString{}, reply.value().path(), dbusRequestInterface, dbusResponse, this, SLOT(onFinished(uint, QVariantMap)));
    } else {
        m_callback(-1, {{QStringLiteral("errorMessage"), reply.error().message()}});
    }
    watcher->deleteLater();
}

void PortalRequest::onFinished(uint code, const QVariantMap &result)
{
    if (m_context) {
        m_callback(code, result);
    }
    deleteLater();
}

class KRDP_NO_EXPORT PortalSession::Private
{
public:
    Server *server = nullptr;

    std::unique_ptr<OrgFreedesktopPortalRemoteDesktopInterface> remoteInterface;
    std::unique_ptr<OrgFreedesktopPortalScreenCastInterface> screencastInterface;

    QDBusObjectPath sessionPath;

    std::unique_ptr<PipeWireRecord> pipeWireRecord;

    bool started = false;

};

QString createHandleToken()
{
    return QStringLiteral("krdp%1").arg(QRandomGenerator::global()->generate());
}

PortalSession::PortalSession(Server *server)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->server = server;

    d->remoteInterface = std::make_unique<OrgFreedesktopPortalRemoteDesktopInterface>(dbusService, dbusPath, QDBusConnection::sessionBus());
    d->screencastInterface = std::make_unique<OrgFreedesktopPortalScreenCastInterface>(dbusService, dbusPath, QDBusConnection::sessionBus());

    if (!d->remoteInterface->isValid() || !d->screencastInterface->isValid()) {
        qCWarning(KRDP) << "Could not connect to Freedesktop Remote Desktop Portal";
        return;
    }

    qCDebug(KRDP) << "Initializing Freedesktop Portal Session";

    auto parameters = QVariantMap{
        {QStringLiteral("handle_token"), createHandleToken()},
        {QStringLiteral("session_handle_token"), createHandleToken()},
    };
    new PortalRequest(d->remoteInterface->CreateSession(parameters), this, &PortalSession::onCreateSession);
}

PortalSession::~PortalSession()
{
    d->pipeWireRecord->setActive(false);

    auto closeMessage = QDBusMessage::createMethodCall(dbusService, d->sessionPath.path(), dbusSessionInterface, QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(closeMessage);
}

void KRdp::PortalSession::sendEvent(QEvent *event)
{
    if (!d->started) {
        return;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
        auto me = static_cast<QMouseEvent *>(event);
        int button = 0;
        if (me->button() == Qt::LeftButton) {
            button = BTN_LEFT;
        } else if (me->button() == Qt::MiddleButton) {
            button = BTN_MIDDLE;
        } else if (me->button() == Qt::RightButton) {
            button = BTN_RIGHT;
        } else {
            qCWarning(KRDP) << "Unsupported mouse button" << me->button();
            return;
        }
        uint state = me->type() == QEvent::MouseButtonPress ? 1 : 0;
        d->remoteInterface->NotifyPointerButton(d->sessionPath, QVariantMap{}, button, state);
        break;
    }
    case QEvent::MouseMove: {
        auto me = static_cast<QMouseEvent *>(event);
        d->remoteInterface->NotifyPointerMotion(d->sessionPath, QVariantMap{}, me->x(), me->y());
        break;
    }
    default:
        break;
    }
}

void PortalSession::onCreateSession(uint code, const QVariantMap &result)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not open a new remote desktop session, error code" << code;
        return;
    }

    d->sessionPath = QDBusObjectPath(result.value(QStringLiteral("session_handle")).toString());

    auto parameters = QVariantMap{
        {QStringLiteral("types"), 7u},
        {QStringLiteral("handle_token"), createHandleToken()},
    };
    new PortalRequest(d->remoteInterface->SelectDevices(d->sessionPath, parameters), this, &PortalSession::onDevicesSelected);
}

void PortalSession::onDevicesSelected(uint code, const QVariantMap & /*result*/)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not select devices for remote desktop session, error code" << code;
        return;
    }

    auto parameters = QVariantMap{
        {QStringLiteral("types"), 1u}, // only MONITOR is supported
        {QStringLiteral("multiple"), false},
        {QStringLiteral("handle_token"), createHandleToken()},
    };
    new PortalRequest(d->screencastInterface->SelectSources(d->sessionPath, parameters), this, &PortalSession::onSourcesSelected);
}

void PortalSession::onSourcesSelected(uint code, const QVariantMap & /*result*/)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not select sources for screencast session, error code" << code;
        return;
    }

    auto parameters = QVariantMap{
        {QStringLiteral("handle_token"), createHandleToken()},
    };
    new PortalRequest(d->remoteInterface->Start(d->sessionPath, QString{}, parameters), this, &PortalSession::onSessionStarted);
}

void KRdp::PortalSession::onSessionStarted(uint code, const QVariantMap &result)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not start screencast session, error code" << code;
        return;
    }

    if (result.value(QStringLiteral("devices")).toUInt() == 0) {
        qCWarning(KRDP) << "No devices were granted" << result;
        return;
    }

    const auto streams = qdbus_cast<QList<PortalSessionStream>>(result.value(QStringLiteral("streams")));
    if (streams.isEmpty()) {
        qCWarning(KRDP) << "No screencast streams supplied";
        return;
    }

    auto watcher = new QDBusPendingCallWatcher(d->screencastInterface->OpenPipeWireRemote(d->sessionPath, QVariantMap{}));
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, streams](QDBusPendingCallWatcher *watcher) {
        auto reply = QDBusReply<QDBusUnixFileDescriptor>(*watcher);
        if (reply.isValid()) {
            auto fd = reply.value();
            d->pipeWireRecord = std::make_unique<PipeWireRecord>();
            d->pipeWireRecord->setNodeId(streams.first().nodeId);
            d->pipeWireRecord->setFd(fd.takeFileDescriptor());
            d->pipeWireRecord->setEncoder("libx264");
            connect(d->pipeWireRecord.get(), &PipeWireRecord::newPacket, this, &PortalSession::onPacketReceived);
            d->pipeWireRecord->setActive(true);
            d->started = true;
            Q_EMIT started();
        } else {
            qCWarning(KRDP) << "Could not open pipewire remote";
        }
        watcher->deleteLater();
    });
}

void PortalSession::onPacketReceived(const QByteArray &data)
{
    VideoFrame frameData;

    // if (frame.cursor) {
    //     auto pwCursor = frame.cursor.value();
    //     VideoFrame::Cursor cursor {
    //         .position = pwCursor.position,
    //         .hotspot = pwCursor.hotspot,
    //         .texture = pwCursor.texture,
    //     };
    //     frameData.cursor = cursor;
    // }
    //
    // QSize dataSize;
    // if (frame.image || frame.dmabuf) {
    //     if (frame.image) {
    //         auto image = frame.image.value();
    //         dataSize = image.size();
    //         frameData.size = image.size();
    //         frameData.data = QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
    //     } else {
    //         // TODO
    //     }
    // }
    //
    // if (frame.damage) {
    //     frameData.damage = frame.damage.value();
    // } else {
    //     frameData.damage = {QRect(QPoint(0, 0), dataSize)};
    // }

    frameData.data = data;

    Q_EMIT frameReceived(frameData);
}

}
