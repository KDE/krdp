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
    struct EisDeviceState {
        bool resumed = false;
        bool emulating = false;
    };

    Server *server = nullptr;

    std::unique_ptr<OrgFreedesktopPortalRemoteDesktopInterface> remoteInterface;
    std::unique_ptr<OrgFreedesktopPortalScreenCastInterface> screencastInterface;

    bool ignoreNextSystemClipboardChange = false;
    bool pipeWireReady = false;
    bool eisConnected = false;
    uint32_t eisSequence = 0;

    QDBusObjectPath sessionPath;
    QString mappingId;

    std::unique_ptr<QSocketNotifier> eisNotifier;
    struct ei *ei = nullptr;
    QHash<struct ei_device *, EisDeviceState> eisDevices;
    struct ei_device *absolutePointerDevice = nullptr;
    struct ei_region *absolutePointerRegion = nullptr;
    struct ei_device *buttonDevice = nullptr;
    struct ei_device *scrollDevice = nullptr;
    struct ei_device *keyboardDevice = nullptr;
    struct ei_device *textDevice = nullptr;
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
    for (auto it = d->eisDevices.begin(); it != d->eisDevices.end(); ++it) {
        if (it->emulating) {
            ei_device_stop_emulating(it.key());
        }
        ei_device_unref(it.key());
    }
    d->eisDevices.clear();

    if (d->ei) {
        ei_disconnect(d->ei);
        while (auto event = ei_get_event(d->ei)) {
            ei_event_unref(event);
        }
        d->ei = ei_unref(d->ei);
    }
    ei_region_unref(d->absolutePointerRegion);
    d->absolutePointerRegion = nullptr;

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
        if (!!d->buttonDevice) {
            return;
        }
        ei_device_button_button(d->buttonDevice, button, me->type() == QEvent::MouseButtonPress);
        ei_device_frame(d->buttonDevice, ei_now(d->ei));
        break;
    }
    case QEvent::MouseMove: {
        if (!d->ei || !d->absolutePointerDevice || !d->absolutePointerRegion || size().isEmpty() || logicalSize().isEmpty()) {
            return;
        }
        auto me = std::static_pointer_cast<QMouseEvent>(event);
        auto position = me->position();
        auto logicalPosition = QPointF{(position.x() / size().width()) * logicalSize().width(), (position.y() / size().height()) * logicalSize().height()};
        const auto absolutePosition = QPointF{
            ei_region_get_x(d->absolutePointerRegion) + ((logicalPosition.x() / logicalSize().width()) * ei_region_get_width(d->absolutePointerRegion)),
            ei_region_get_y(d->absolutePointerRegion) + ((logicalPosition.y() / logicalSize().height()) * ei_region_get_height(d->absolutePointerRegion)),
        };
        ei_device_pointer_motion_absolute(d->absolutePointerDevice, absolutePosition.x(), absolutePosition.y());
        ei_device_frame(d->absolutePointerDevice, ei_now(d->ei));
        break;
    }
    case QEvent::Wheel: {
        if (!d->ei || !d->scrollDevice) {
            return;
        }
        auto we = std::static_pointer_cast<QWheelEvent>(event);
        auto delta = we->angleDelta();
        ei_device_scroll_discrete(d->scrollDevice, delta.x(), -delta.y());
        ei_device_frame(d->scrollDevice, ei_now(d->ei));
        break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto ke = std::static_pointer_cast<QKeyEvent>(event);
        const auto isPress = event->type() == QEvent::KeyPress;

        if (ke->nativeScanCode()) {
            if (!d->ei || !d->keyboardDevice) {
                return;
            }
            ei_device_keyboard_key(d->keyboardDevice, ke->nativeScanCode(), isPress);
            ei_device_frame(d->keyboardDevice, ei_now(d->ei));
        } else if (ke->nativeVirtualKey()) {
            if (!d->ei || !d->textDevice) {
                return;
            }
            ei_device_text_keysym(d->textDevice, ke->nativeVirtualKey(), isPress);
            ei_device_frame(d->textDevice, ei_now(d->ei));
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
            if (streamIndex >= streams.size()) {
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
        auto cleanup = qScopeGuard([watcher] {
            watcher->deleteLater();
        });

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

        d->eisNotifier = std::make_unique<QSocketNotifier>(ei_get_fd(d->ei), QSocketNotifier::Read, this);
        connect(d->eisNotifier.get(), &QSocketNotifier::activated, this, &PortalSession::onEisReadyRead);
    });
}

void PortalSession::processEisEvents()
{
    auto assignAbsolutePointerDevice = [this](struct ei_device *device) {
        struct ei_region *selectedRegion = nullptr;
        struct ei_region *fallbackRegion = nullptr;
        for (size_t i = 0; auto region = ei_device_get_region(device, i); ++i) {
            if (!fallbackRegion) {
                fallbackRegion = region;
            }
            const auto mappingId = ei_region_get_mapping_id(region);
            if (!d->mappingId.isEmpty() && mappingId && QString::fromUtf8(mappingId) == d->mappingId) {
                selectedRegion = region;
                break;
            }
        }

        if (!selectedRegion) {
            selectedRegion = fallbackRegion;
        }
        if (!selectedRegion) {
            return;
        }

        const auto selectedMappingId = ei_region_get_mapping_id(selectedRegion);
        const auto currentMappingId = d->absolutePointerRegion ? ei_region_get_mapping_id(d->absolutePointerRegion) : nullptr;
        if (d->absolutePointerDevice && currentMappingId && d->mappingId == QString::fromUtf8(currentMappingId)
            && (!selectedMappingId || d->mappingId != QString::fromUtf8(selectedMappingId))) {
            return;
        }

        d->absolutePointerDevice = device;
        if (d->absolutePointerRegion) {
            d->absolutePointerRegion = ei_region_unref(d->absolutePointerRegion);
        }
        d->absolutePointerRegion = ei_region_ref(selectedRegion);
    };

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
            d->eisDevices.insert(ei_device_ref(device), {.resumed = false, .emulating = false});
            if (ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE)) {
                assignAbsolutePointerDevice(device);
            }
            if (!d->buttonDevice && ei_device_has_capability(device, EI_DEVICE_CAP_BUTTON)) {
                d->buttonDevice = device;
            }
            if (!d->scrollDevice && ei_device_has_capability(device, EI_DEVICE_CAP_SCROLL)) {
                d->scrollDevice = device;
            }
            if (!d->keyboardDevice && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
                d->keyboardDevice = device;
            }
            if (!d->textDevice && ei_device_has_capability(device, EI_DEVICE_CAP_TEXT)) {
                d->textDevice = device;
            }
            break;
        case EI_EVENT_DEVICE_REMOVED:
            if (d->absolutePointerDevice == device) {
                d->absolutePointerDevice = nullptr;
                if (d->absolutePointerRegion) {
                    d->absolutePointerRegion = ei_region_unref(d->absolutePointerRegion);
                }
            }
            if (d->buttonDevice == device) {
                d->buttonDevice = nullptr;
            }
            if (d->scrollDevice == device) {
                d->scrollDevice = nullptr;
            }
            if (d->keyboardDevice == device) {
                d->keyboardDevice = nullptr;
            }
            if (d->textDevice == device) {
                d->textDevice = nullptr;
            }
            if (auto it = d->eisDevices.find(device); it != d->eisDevices.end()) {
                ei_device_unref(it.key());
                d->eisDevices.erase(it);
            }
            break;
        case EI_EVENT_DEVICE_PAUSED:
            if (auto it = d->eisDevices.find(device); it != d->eisDevices.end()) {
                it->resumed = false;
                it->emulating = false;
            }
            break;
        case EI_EVENT_DEVICE_RESUMED:
            if (auto it = d->eisDevices.find(device); it != d->eisDevices.end()) {
                it->resumed = true;
                if (!it->emulating) {
                    ei_device_start_emulating(device, ++d->eisSequence);
                    it->emulating = true;
                }
            }
            break;
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
