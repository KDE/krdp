// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PortalSession.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QQueue>

#include <linux/input.h>

#include <KPipeWire/PipeWireEncodedStream>

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

    std::unique_ptr<PipeWireEncodedStream> encodedStream;

    bool started = false;
    bool enabled = false;
    QSize size;
    QSize logicalSize;
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

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PortalSession::updateScreenLayout);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PortalSession::updateScreenLayout);
    updateScreenLayout();

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
    if (d->encodedStream) {
        d->encodedStream->setActive(false);
    }

    auto closeMessage = QDBusMessage::createMethodCall(dbusService, d->sessionPath.path(), dbusSessionInterface, QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(closeMessage);

    qCDebug(KRDP) << "Closing Freedesktop Portal Session";
}

bool PortalSession::streamingEnabled() const
{
    if (d->encodedStream) {
        return d->encodedStream->isActive();
    }
    return false;
}

void PortalSession::setStreamingEnabled(bool enable)
{
    d->enabled = enable;
    if (d->encodedStream) {
        d->encodedStream->setActive(enable);
    }
}

void PortalSession::setVideoFrameRate(quint32 framerate)
{
    if (d->encodedStream) {
        d->encodedStream->setMaxFramerate({framerate, 1});
    }
}

void PortalSession::sendEvent(QEvent *event)
{
    if (!d->started || !d->encodedStream) {
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
        d->remoteInterface->NotifyPointerMotionAbsolute(d->sessionPath, QVariantMap{}, d->encodedStream->nodeId(), position.x(), position.y());
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
            qCDebug(KRDP) << "Started Freedesktop Portal session";
            auto fd = reply.value();
            d->encodedStream = std::make_unique<PipeWireEncodedStream>();
            d->encodedStream->setNodeId(streams.first().nodeId);
            d->encodedStream->setFd(fd.takeFileDescriptor());
            d->encodedStream->setEncoder(PipeWireEncodedStream::H264Baseline);
            connect(d->encodedStream.get(), &PipeWireEncodedStream::newPacket, this, &PortalSession::onPacketReceived);
            connect(d->encodedStream.get(), &PipeWireEncodedStream::sizeChanged, this, [this](const QSize &size) {
                d->size = size;
            });
            connect(d->encodedStream.get(), &PipeWireEncodedStream::cursorChanged, this, &PortalSession::cursorUpdate);
            d->started = true;
            Q_EMIT started();
            d->encodedStream->setActive(d->enabled);
        } else {
            qCWarning(KRDP) << "Could not open pipewire remote";
        }
        watcher->deleteLater();
    });
}

void PortalSession::onPacketReceived(const PipeWireEncodedStream::Packet &data)
{
    VideoFrame frameData;

    frameData.size = d->size;
    frameData.data = data.data();
    frameData.isKeyFrame = data.isKeyFrame();

    Q_EMIT frameReceived(frameData);
}

void PortalSession::updateScreenLayout()
{
    int logicalSurfaceWidth = 0;
    int logicalSurfaceHeight = 0;
    const auto screens = QGuiApplication::screens();
    for (auto screen : screens) {
        logicalSurfaceWidth += screen->geometry().width();
        logicalSurfaceHeight += screen->geometry().height();
    }

    d->logicalSize = QSize{logicalSurfaceWidth, logicalSurfaceHeight};
}
}
