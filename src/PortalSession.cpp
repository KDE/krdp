// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PortalSession.h"

#include <QGuiApplication>
#include <QMimeData>
#include <QMouseEvent>
#include <QQueue>
#include <QScopeGuard>
#include <QSocketNotifier>

#include <libei.h>
#include <linux/input.h>

#include <KConfigGroup>
#include <KSharedConfig>
#include <KSystemClipboard>

#include "PortalSession_p.h"
#include "krdp_logging.h"
#include "xdp_dbus_remotedesktop_interface.h"
#include "xdp_dbus_screencast_interface.h"

using namespace Qt::StringLiterals;

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
    class EiDevice
    {
    public:
        explicit EiDevice(struct ei_device *device)
            : m_device(ei_device_ref(device))
        {
        }

        virtual ~EiDevice()
        {
            ei_device_unref(m_device);
        }

        EiDevice(const EiDevice &) = delete;
        EiDevice &operator=(const EiDevice &) = delete;

        [[nodiscard]] struct ei_device *device() const
        {
            return m_device;
        }

    private:
        struct ei_device *m_device = nullptr;
    };

    class EisPointerDevice : public EiDevice
    {
    public:
        struct Region {
            QRectF rect;
            qreal scale = 1.0;
        };

        explicit EisPointerDevice(struct ei_device *device)
            : EiDevice(device)
        {
            for (size_t i = 0; auto eiRegion = ei_device_get_region(device, i); ++i) {
                const Region region{
                    .rect = QRectF{static_cast<qreal>(ei_region_get_x(eiRegion)),
                                   static_cast<qreal>(ei_region_get_y(eiRegion)),
                                   static_cast<qreal>(ei_region_get_width(eiRegion)),
                                   static_cast<qreal>(ei_region_get_height(eiRegion))},
                    .scale = static_cast<qreal>(ei_region_get_physical_scale(eiRegion)),
                };
                const auto mappingId = ei_region_get_mapping_id(eiRegion);
                if (mappingId) {
                    regions.insert(QString::fromUtf8(mappingId), region);
                }
            }
        }
        QHash<QString, Region> regions;

        EisPointerDevice(const EisPointerDevice &) = delete;
        EisPointerDevice &operator=(const EisPointerDevice &) = delete;

        [[nodiscard]] std::optional<Region> regionForMapping(const QString &mappingId) const
        {
            const auto it = regions.constFind(mappingId);
            if (it != regions.cend()) {
                return *it;
            }

            return std::nullopt;
        }
    };

    Server *server = nullptr;

    std::unique_ptr<OrgFreedesktopPortalRemoteDesktopInterface> remoteInterface;
    std::unique_ptr<OrgFreedesktopPortalScreenCastInterface> screencastInterface;

    bool ignoreNextSystemClipboardChange = false;
    bool pipeWireReady = false;
    bool eisConnected = false;

    QDBusObjectPath sessionPath;
    QString mappingId;

    std::unique_ptr<QSocketNotifier> eisNotifier;
    struct ei *ei = nullptr;
    std::vector<std::unique_ptr<EisPointerDevice>> pointerDevices;
    std::unique_ptr<EiDevice> keyboardDevice;
    std::unique_ptr<EiDevice> textDevice;
};

QString createHandleToken()
{
    return QStringLiteral("krdp%1").arg(QRandomGenerator::global()->generate());
}

PortalSession::PortalSession()
    : AbstractSession()
    , d(std::make_unique<Private>())
{
    d->remoteInterface = std::make_unique<OrgFreedesktopPortalRemoteDesktopInterface>(dbusService, dbusPath, QDBusConnection::sessionBus());
    d->screencastInterface = std::make_unique<OrgFreedesktopPortalScreenCastInterface>(dbusService, dbusPath, QDBusConnection::sessionBus());

    connect(KSystemClipboard::instance(), &KSystemClipboard::changed, this, [this](auto mode) {
        if (mode != QClipboard::Clipboard) {
            return;
        }

        auto data = KSystemClipboard::instance()->mimeData(mode);
        if (!data) {
            return;
        }

        qCDebug(KRDP) << "Clipboard formats:" << data->formats() << "hasText:" << data->hasText();

        // KSystemClipboard takes ownership of any QMimeData passed to it but
        // does not relinquish ownership over anything it returns. So manually
        // copy over the contents to a new instance of QMimeData so we can keep
        // the semantics the same.
        //
        // Only copy text data here. Fetching arbitrary clipboard MIME payloads
        // can block for a long time on Wayland/XWayland targets such as
        // SAVE_TARGETS, which freezes the server's main thread. The RDP
        // clipboard implementation currently only exposes text anyway.
        auto newData = new QMimeData();
        if (data->hasText()) {
            newData->setText(data->text());
        } else {
            qCDebug(KRDP) << "Ignoring non-text clipboard update with formats" << data->formats();
        }

        Q_EMIT clipboardDataChanged(newData);
    });

    if (!d->remoteInterface->isValid() || !d->screencastInterface->isValid()) {
        qCWarning(KRDP) << "Could not connect to Freedesktop Remote Desktop Portal";
        return;
    }
}

PortalSession::~PortalSession()
{
    if (d->ei) {
        ei_disconnect(d->ei);
        while (auto event = ei_get_event(d->ei)) {
            ei_event_unref(event);
        }
        d->ei = ei_unref(d->ei);
    }

    if (d->sessionPath.path().isEmpty()) {
        qCDebug(KRDP) << "No portal session to close (session was never created)";
        return;
    }

    auto closeMessage = QDBusMessage::createMethodCall(dbusService, d->sessionPath.path(), dbusSessionInterface, QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(closeMessage);

    qCDebug(KRDP) << "Closing Freedesktop Portal Session";
}

void PortalSession::start()
{
    qCDebug(KRDP) << "Initializing Freedesktop Portal Session";

    auto parameters = QVariantMap{
        {QStringLiteral("handle_token"), createHandleToken()},
        {QStringLiteral("session_handle_token"), createHandleToken()},
    };
    new PortalRequest(d->remoteInterface->CreateSession(parameters), this, &PortalSession::onCreateSession);
}

void PortalSession::sendEvent(const std::shared_ptr<QEvent> &event)
{
    if (!isStarted()) {
        return;
    }

    const auto findPointerDeviceWithCapability = [this](enum ei_device_capability capability) -> Private::EisPointerDevice * {
        for (const auto &pointerDevice : d->pointerDevices) {
            if (ei_device_has_capability(pointerDevice->device(), capability)) {
                return pointerDevice.get();
            }
        }

        return nullptr;
    };

    const auto findPointerDeviceForMappingId =
        [this](const QString &mappingId) -> std::optional<std::pair<Private::EisPointerDevice *, Private::EisPointerDevice::Region>> {
        for (const auto &pointerDevice : d->pointerDevices) {
            const auto region = pointerDevice->regionForMapping(mappingId);
            if (region.has_value()) {
                return std::pair{pointerDevice.get(), *region};
            }
        }

        return std::nullopt;
    };

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
        auto me = std::static_pointer_cast<QMouseEvent>(event);
        int button = 0;
        if (me->button() == Qt::LeftButton) {
            button = BTN_LEFT;
        } else if (me->button() == Qt::MiddleButton) {
            button = BTN_MIDDLE;
        } else if (me->button() == Qt::RightButton) {
            button = BTN_RIGHT;
        } else if (me->button() == Qt::BackButton) {
            button = BTN_SIDE;
        } else if (me->button() == Qt::ForwardButton) {
            button = BTN_EXTRA;
        } else {
            qCWarning(KRDP) << "Unsupported mouse button" << me->button();
            return;
        }
        auto pointerDevice = d->ei ? findPointerDeviceWithCapability(EI_DEVICE_CAP_BUTTON) : nullptr;
        if (!pointerDevice) {
            qCWarning(KRDP) << "Mouse press event received but no button devices are available.";

            return;
        }
        ei_device_button_button(pointerDevice->device(), button, me->type() == QEvent::MouseButtonPress);
        ei_device_frame(pointerDevice->device(), ei_now(d->ei));
        break;
    }
    case QEvent::MouseMove: {
        if (!d->ei || d->pointerDevices.empty()) {
            qCWarning(KRDP) << "Mouse move event received but no pointer devices are available.";
            return;
        }
        if (size().isEmpty()) {
            qCWarning(KRDP) << "Mouse move event received but stream size is unknown.";
            return;
        }
        auto me = std::static_pointer_cast<QMouseEvent>(event);

        Private::EisPointerDevice *pointerDevice;
        QPointF devicePosition;
        QPointF streamPosition = me->position();

        if (!d->mappingId.isEmpty()) {
            const auto mappedPointerDevice = findPointerDeviceForMappingId(d->mappingId);
            if (!mappedPointerDevice) {
                qCWarning(KRDP) << "Mouse move event whilst screen has explicit mapping, but no associated device found.";
                return;
            }

            const auto &[mappedDevice, region] = *mappedPointerDevice;
            pointerDevice = mappedDevice;

            auto logicalStreamPosition =
                QPointF{(streamPosition.x() / size().width()) * region.rect.width(), (streamPosition.y() / size().height()) * region.rect.height()};
            devicePosition = QPointF{
                region.rect.x() + logicalStreamPosition.x(),
                region.rect.y() + logicalStreamPosition.y(),
            };
        } else {
            pointerDevice = findPointerDeviceWithCapability(EI_DEVICE_CAP_POINTER_ABSOLUTE);
            devicePosition = me->position();
        }

        ei_device_pointer_motion_absolute(pointerDevice->device(), devicePosition.x(), devicePosition.y());
        ei_device_frame(pointerDevice->device(), ei_now(d->ei));
        break;
    }
    case QEvent::Wheel: {
        auto pointerDevice = d->ei ? findPointerDeviceWithCapability(EI_DEVICE_CAP_SCROLL) : nullptr;
        if (!pointerDevice) {
            return;
        }
        auto we = std::static_pointer_cast<QWheelEvent>(event);
        auto delta = we->angleDelta();
        ei_device_scroll_discrete(pointerDevice->device(), delta.x(), -delta.y());
        ei_device_frame(pointerDevice->device(), ei_now(d->ei));
        break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto ke = std::static_pointer_cast<QKeyEvent>(event);
        const auto isPress = event->type() == QEvent::KeyPress;

        if (ke->nativeScanCode()) {
            if (!d->ei || !d->keyboardDevice) {
                qCWarning(KRDP) << "Keyboard event received but no keyboard device is available.";
                return;
            }
            ei_device_keyboard_key(d->keyboardDevice->device(), ke->nativeScanCode(), isPress);
            ei_device_frame(d->keyboardDevice->device(), ei_now(d->ei));
        } else if (ke->nativeVirtualKey()) {
            if (!d->ei || !d->textDevice) {
                qCWarning(KRDP) << "Keyboard event received but no text device is available.";
                return;
            }
            ei_device_text_keysym(d->textDevice->device(), ke->nativeVirtualKey(), isPress);
            ei_device_frame(d->textDevice->device(), ei_now(d->ei));
        }
        break;
    }
    default:
        break;
    }
}

void PortalSession::setClipboardData(std::unique_ptr<QMimeData> data)
{
    // KSystemClipboard takes ownership
    if (data) {
        KSystemClipboard::instance()->setMimeData(data.release(), QClipboard::Clipboard);
    } else {
        KSystemClipboard::instance()->clear(QClipboard::Clipboard);
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

    static const uint PermissionsPersistUntilExplicitlyRevoked = 2;

    auto parameters = QVariantMap{
        {QStringLiteral("types"), 7u},
        {QStringLiteral("handle_token"), createHandleToken()},
        {QStringLiteral("persist_mode"), PermissionsPersistUntilExplicitlyRevoked},
    };
    // name is set explicitly as this is also used by the KCM
    KConfigGroup restorationGroup = KSharedConfig::openStateConfig(QStringLiteral("krdp-serverstaterc"))->group(QStringLiteral("General"));
    QString restoreToken = restorationGroup.readEntry(QStringLiteral("restorationToken"));

    // this is a compatibility path for krdp < 6.3 that used a different name and in .config
    // in 6.4 onwards it can be killed
    if (restoreToken.isEmpty()) {
        KConfigGroup restorationGroup = KSharedConfig::openConfig(QStringLiteral("krdp-serverrc"))->group(QStringLiteral("General"));
        restoreToken = restorationGroup.readEntry(QStringLiteral("restorationToken"));
    } // end compat

    if (!restoreToken.isEmpty()) {
        parameters[QStringLiteral("restore_token")] = restoreToken;
    }

    new PortalRequest(d->remoteInterface->SelectDevices(d->sessionPath, parameters), this, &PortalSession::onDevicesSelected);
}

void PortalSession::onDevicesSelected(uint code, const QVariantMap & /*result*/)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not select devices for remote desktop session, error code" << code;
        Q_EMIT error();
        return;
    }

    QVariantMap parameters;
    if (virtualMonitor()) {
        parameters = {{QStringLiteral("types"), 4u}}; // VIRTUAL
    } else {
        parameters = {{QStringLiteral("types"), 1u}, // MONITOR
                      {QStringLiteral("multiple"), activeStream().has_value()}};
    }
    parameters[QStringLiteral("cursor_mode")] = 4u; // Metadata

    new PortalRequest(d->screencastInterface->SelectSources(d->sessionPath, parameters), this, &PortalSession::onSourcesSelected);
}

void PortalSession::onSourcesSelected(uint code, const QVariantMap & /*result*/)
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

    KConfigGroup restorationGroup = KSharedConfig::openStateConfig(QStringLiteral("krdp-serverstaterc"))->group(QStringLiteral("General"));
    restorationGroup.writeEntry("restorationToken", result.value(QStringLiteral("restore_token")));

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
            auto streamIndex = activeStream().value_or(0);
            if (streamIndex < 0 || streamIndex >= streams.size()) {
                qCWarning(KRDP) << "Requested monitor index out of range, using first monitor";
                setActiveStream(0);
                streamIndex = 0;
            }
            auto stream = streams.at(streamIndex);

            setLogicalSize(qdbus_cast<QSize>(stream.map.value(u"size"_s)));
            d->mappingId = stream.map.value(u"mapping_id"_s).toString();

            auto fd = reply.value();
            setNodeId(stream.nodeId);
            setPipeWireFd(fd.takeFileDescriptor());
            d->pipeWireReady = true;
            QDBusConnection::sessionBus().connect(u"org.freedesktop.portal.Desktop"_s,
                                                  d->sessionPath.path(),
                                                  u"org.freedesktop.portal.Session"_s,
                                                  u"Closed"_s,
                                                  this,
                                                  SLOT(onSessionClosed()));
            qCDebug(KRDP) << "Started Freedesktop Portal session";
            setStarted(true);
        } else {
            qCWarning(KRDP) << "Could not open pipewire remote";
            Q_EMIT error();
        }
        watcher->deleteLater();
    });

    connectToEis();
}

void PortalSession::onSessionClosed()
{
    qCWarning(KRDP) << "Portal session was closed!";
    Q_EMIT error();
}

void PortalSession::connectToEis()
{
    auto watcher = new QDBusPendingCallWatcher(d->remoteInterface->ConnectToEIS(d->sessionPath, QVariantMap{}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        if (reply.isError()) {
            qCWarning(KRDP) << "Could not connect portal session to EIS:" << reply.error().message();
            Q_EMIT error();
            return;
        }

        d->ei = ei_new_sender(this);
        if (!d->ei) {
            qCWarning(KRDP) << "Could not create libei sender context";
            Q_EMIT error();
            return;
        }

        ei_configure_name(d->ei, "krdp");
        if (const auto rc = ei_setup_backend_fd(d->ei, reply.value().takeFileDescriptor()); rc != 0) {
            qCWarning(KRDP) << "Could not set up libei backend:" << rc;
            d->ei = ei_unref(d->ei);
            Q_EMIT error();
            return;
        }

        d->eisNotifier = std::make_unique<QSocketNotifier>(ei_get_fd(d->ei), QSocketNotifier::Read);
        connect(d->eisNotifier.get(), &QSocketNotifier::activated, this, &PortalSession::onEisReadyRead);
    });
}

void PortalSession::processEisEvents()
{
    while (auto event = ei_get_event(d->ei)) {
        auto cleanup = qScopeGuard([event] {
            ei_event_unref(event);
        });

        const auto eventType = ei_event_get_type(event);
        auto device = ei_event_get_device(event);

        switch (eventType) {
        case EI_EVENT_CONNECT:
            d->eisConnected = true;
            break;
        case EI_EVENT_DISCONNECT:
            qCWarning(KRDP) << "Portal EIS connection disconnected";
            d->eisConnected = false;
            Q_EMIT error();
            break;
        case EI_EVENT_SEAT_ADDED: {
            auto seat = ei_event_get_seat(event);
            ei_seat_bind_capabilities(seat,
                                      EI_DEVICE_CAP_POINTER_ABSOLUTE,
                                      EI_DEVICE_CAP_BUTTON,
                                      EI_DEVICE_CAP_SCROLL,
                                      EI_DEVICE_CAP_KEYBOARD,
                                      EI_DEVICE_CAP_TEXT,
                                      nullptr);
            ei_seat_request_device_with_capabilities(seat,
                                                     EI_DEVICE_CAP_POINTER_ABSOLUTE,
                                                     EI_DEVICE_CAP_BUTTON,
                                                     EI_DEVICE_CAP_SCROLL,
                                                     EI_DEVICE_CAP_KEYBOARD,
                                                     EI_DEVICE_CAP_TEXT,
                                                     nullptr);
            break;
        }
        case EI_EVENT_DEVICE_ADDED:
            if (ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE)) {
                d->pointerDevices.push_back(std::make_unique<Private::EisPointerDevice>(device));
            }
            if (!d->keyboardDevice && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
                d->keyboardDevice.reset(new Private::EiDevice(device));
            }
            if (!d->textDevice && ei_device_has_capability(device, EI_DEVICE_CAP_TEXT)) {
                d->textDevice.reset(new Private::EiDevice(device));
            }
            ei_device_start_emulating(device, ei_now(d->ei));
            break;
        case EI_EVENT_DEVICE_REMOVED:
            std::erase_if(d->pointerDevices, [device](const auto &pointerDevice) {
                return pointerDevice->device() == device;
            });
            if (d->keyboardDevice && d->keyboardDevice->device() == device) {
                d->keyboardDevice.reset();
            }
            if (d->textDevice && d->textDevice->device() == device) {
                d->textDevice.reset();
            }
            break;
        case EI_EVENT_DEVICE_RESUMED:
            ei_device_start_emulating(device, ei_now(d->ei));
        default:
            break;
        }
    }
}

void PortalSession::onEisReadyRead()
{
    if (!d->ei) {
        return;
    }

    ei_dispatch(d->ei);
    processEisEvents();
}
}

#include "moc_PortalSession_p.cpp"

#include "moc_PortalSession.cpp"
