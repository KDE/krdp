// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PortalSession.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QQueue>

#include <linux/input.h>

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
};

QString createHandleToken()
{
    return QStringLiteral("krdp%1").arg(QRandomGenerator::global()->generate());
}

PortalSession::PortalSession(Server *server)
    : AbstractSession(server)
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
    // Make sure to clear any modifier keys that were pressed when the session closed, otherwise
    // we risk those keys getting stuck and the original session becoming unusable.
    for (auto keycode : {KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTMETA, KEY_RIGHTMETA}) {
        auto call = d->remoteInterface->NotifyKeyboardKeycode(d->sessionPath, QVariantMap{}, keycode, 0);
        call.waitForFinished();
    }

    auto closeMessage = QDBusMessage::createMethodCall(dbusService, d->sessionPath.path(), dbusSessionInterface, QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(closeMessage);

    qCDebug(KRDP) << "Closing Freedesktop Portal Session";
}

void PortalSession::sendEvent(QEvent *event)
{
    auto encodedStream = stream();
    if (!encodedStream || !encodedStream->isActive()) {
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
        auto position = me->globalPosition();
        auto logicalPosition = QPointF{(position.x() / size().width()) * logicalSize().width(), (position.y() / size().height()) * logicalSize().height()};
        d->remoteInterface->NotifyPointerMotionAbsolute(d->sessionPath, QVariantMap{}, encodedStream->nodeId(), logicalPosition.x(), logicalPosition.y());
        break;
    }
    case QEvent::Wheel: {
        auto we = static_cast<QWheelEvent *>(event);
        d->remoteInterface->NotifyPointerAxisDiscrete(d->sessionPath, QVariantMap{}, 0, we->angleDelta().y() / 120);
        break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto ke = static_cast<QKeyEvent *>(event);
        auto state = ke->type() == QEvent::KeyPress ? 1 : 0;

        if (ke->nativeScanCode()) {
            d->remoteInterface->NotifyKeyboardKeycode(d->sessionPath, QVariantMap{}, ke->nativeScanCode(), state);
        } else {
            d->remoteInterface->NotifyKeyboardKeysym(d->sessionPath, QVariantMap{}, ke->nativeVirtualKey(), state);
        }
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
        Q_EMIT error();
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
        Q_EMIT error();
        return;
    }

    auto parameters = QVariantMap{
        {QStringLiteral("types"), 1u}, // only MONITOR is supported
        {QStringLiteral("multiple"), activeStream() >= 0},
        {QStringLiteral("handle_token"), createHandleToken()},
    };
    new PortalRequest(d->screencastInterface->SelectSources(d->sessionPath, parameters), this, &PortalSession::onSourcesSelected);
}

void PortalSession::onSourcesSelected(uint code, const QVariantMap &result)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not select sources for screencast session, error code" << code;
        Q_EMIT error();
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
        Q_EMIT error();
        return;
    }

    if (result.value(QStringLiteral("devices")).toUInt() == 0) {
        qCWarning(KRDP) << "No devices were granted" << result;
        Q_EMIT error();
        return;
    }

    const auto streams = qdbus_cast<QList<PortalSessionStream>>(result.value(QStringLiteral("streams")));
    if (streams.isEmpty()) {
        qCWarning(KRDP) << "No screencast streams supplied";
        Q_EMIT error();
        return;
    }

    auto watcher = new QDBusPendingCallWatcher(d->screencastInterface->OpenPipeWireRemote(d->sessionPath, QVariantMap{}));
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, streams](QDBusPendingCallWatcher *watcher) {
        auto reply = QDBusReply<QDBusUnixFileDescriptor>(*watcher);
        if (reply.isValid()) {
            qCDebug(KRDP) << "Started Freedesktop Portal session";

            if (activeStream() >= streams.size()) {
                qCWarning(KRDP) << "Requested monitor index out of range, using first monitor";
                setActiveStream(0);
            }
            auto stream = streams.at(activeStream() >= 0 ? activeStream() : 0);

            setLogicalSize(qdbus_cast<QSize>(stream.map.value(u"size"_qs)));
            auto fd = reply.value();
            auto encodedStream = this->stream();
            encodedStream->setNodeId(stream.nodeId);
            encodedStream->setFd(fd.takeFileDescriptor());
            encodedStream->setEncoder(PipeWireEncodedStream::H264Baseline);
            connect(encodedStream, &PipeWireEncodedStream::newPacket, this, &PortalSession::onPacketReceived);
            connect(encodedStream, &PipeWireEncodedStream::sizeChanged, this, &PortalSession::setSize);
            connect(encodedStream, &PipeWireEncodedStream::cursorChanged, this, &PortalSession::cursorUpdate);
            setStarted(true);
        } else {
            qCWarning(KRDP) << "Could not open pipewire remote";
            Q_EMIT error();
        }
        watcher->deleteLater();
    });
}

void PortalSession::onPacketReceived(const PipeWireEncodedStream::Packet &data)
{
    VideoFrame frameData;

    frameData.size = size();
    frameData.data = data.data();
    frameData.isKeyFrame = data.isKeyFrame();

    Q_EMIT frameReceived(frameData);
}

}

#include "moc_PortalSession_p.cpp"

#include "moc_PortalSession.cpp"
