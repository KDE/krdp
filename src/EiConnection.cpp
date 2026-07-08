// SPDX-FileCopyrightText: 2026 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "EiConnection.h"

#include <QMouseEvent>
#include <QScopeGuard>
#include <QSocketNotifier>
#include <QWheelEvent>

#include <libei.h>
#include <linux/input.h>

#include "krdp_logging.h"

namespace KRdp
{

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
                m_regions.insert(QString::fromUtf8(mappingId), region);
            }
        }
    }

    EisPointerDevice(const EisPointerDevice &) = delete;
    EisPointerDevice &operator=(const EisPointerDevice &) = delete;

    [[nodiscard]] std::optional<Region> regionForMapping(const QString &mappingId) const
    {
        const auto it = m_regions.constFind(mappingId);
        if (it != m_regions.cend()) {
            return *it;
        }

        return std::nullopt;
    }

private:
    QHash<QString, Region> m_regions;
};

EiConnection::EiConnection(int fd, QObject *parent)
    : QObject(parent)
{
    m_ei = ei_new_sender(this);
    if (!m_ei) {
        qCWarning(KRDP) << "Could not create libei sender context";
        return;
    }

    ei_configure_name(m_ei, "krdp");
    if (const auto rc = ei_setup_backend_fd(m_ei, fd); rc != 0) {
        qCWarning(KRDP) << "Could not set up libei backend:" << rc;
        m_ei = ei_unref(m_ei);
        return;
    }

    m_eisNotifier = std::make_unique<QSocketNotifier>(ei_get_fd(m_ei), QSocketNotifier::Read);
    connect(m_eisNotifier.get(), &QSocketNotifier::activated, this, &EiConnection::onEisReadyRead);
}

EiConnection::~EiConnection()
{
    if (!m_ei) {
        return;
    }

    ei_disconnect(m_ei);
    while (auto event = ei_get_event(m_ei)) {
        ei_event_unref(event);
    }
    m_ei = ei_unref(m_ei);
}

bool EiConnection::isValid() const
{
    return m_ei;
}

void EiConnection::sendEvent(const std::shared_ptr<QEvent> &event, const QSize &streamSize, const QString &mappingId)
{
    const auto findPointerDeviceWithCapability = [this](enum ei_device_capability capability) -> EisPointerDevice * {
        for (const auto &pointerDevice : m_pointerDevices) {
            if (ei_device_has_capability(pointerDevice->device(), capability)) {
                return pointerDevice.get();
            }
        }

        return nullptr;
    };

    const auto findPointerDeviceForMappingId =
        [this](const QString &mappingId) -> std::optional<std::pair<EisPointerDevice *, EisPointerDevice::Region>> {
        for (const auto &pointerDevice : m_pointerDevices) {
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
        auto pointerDevice = m_ei ? findPointerDeviceWithCapability(EI_DEVICE_CAP_BUTTON) : nullptr;
        if (!pointerDevice) {
            qCWarning(KRDP) << "Mouse press event received but no button devices are available.";
            return;
        }
        ei_device_button_button(pointerDevice->device(), button, me->type() == QEvent::MouseButtonPress);
        ei_device_frame(pointerDevice->device(), ei_now(m_ei));
        break;
    }
    case QEvent::MouseMove: {
        if (!m_ei || m_pointerDevices.empty()) {
            qCWarning(KRDP) << "Mouse move event received but no pointer devices are available.";
            return;
        }
        if (streamSize.isEmpty()) {
            qCWarning(KRDP) << "Mouse move event received but stream size is unknown.";
            return;
        }
        auto me = std::static_pointer_cast<QMouseEvent>(event);

        EisPointerDevice *pointerDevice;
        QPointF devicePosition;
        QPointF streamPosition = me->position();

        if (!mappingId.isEmpty()) {
            const auto mappedPointerDevice = findPointerDeviceForMappingId(mappingId);
            if (!mappedPointerDevice) {
                qCWarning(KRDP) << "Mouse move event whilst screen has explicit mapping, but no associated device found.";
                return;
            }

            const auto &[mappedDevice, region] = *mappedPointerDevice;
            pointerDevice = mappedDevice;
            auto logicalStreamPosition =
                QPointF{(streamPosition.x() / streamSize.width()) * region.rect.width(), (streamPosition.y() / streamSize.height()) * region.rect.height()};
            devicePosition = QPointF{region.rect.x() + logicalStreamPosition.x(), region.rect.y() + logicalStreamPosition.y()};
        } else {
            pointerDevice = findPointerDeviceWithCapability(EI_DEVICE_CAP_POINTER_ABSOLUTE);
            devicePosition = me->position();
        }

        ei_device_pointer_motion_absolute(pointerDevice->device(), devicePosition.x(), devicePosition.y());
        ei_device_frame(pointerDevice->device(), ei_now(m_ei));
        break;
    }
    case QEvent::Wheel: {
        auto pointerDevice = m_ei ? findPointerDeviceWithCapability(EI_DEVICE_CAP_SCROLL) : nullptr;
        if (!pointerDevice) {
            return;
        }
        auto we = std::static_pointer_cast<QWheelEvent>(event);
        auto delta = we->angleDelta();
        ei_device_scroll_discrete(pointerDevice->device(), delta.x(), delta.y());
        ei_device_frame(pointerDevice->device(), ei_now(m_ei));
        break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto ke = std::static_pointer_cast<QKeyEvent>(event);
        const auto isPress = event->type() == QEvent::KeyPress;

        if (ke->nativeScanCode()) {
            if (!m_ei || !m_keyboardDevice) {
                qCWarning(KRDP) << "Keyboard event received but no keyboard device is available.";
                return;
            }
            ei_device_keyboard_key(m_keyboardDevice->device(), ke->nativeScanCode(), isPress);
            ei_device_frame(m_keyboardDevice->device(), ei_now(m_ei));
        } else if (ke->nativeVirtualKey()) {
            if (!m_ei || !m_textDevice) {
                qCWarning(KRDP) << "Keyboard event received but no text device is available.";
                return;
            }
            ei_device_text_keysym(m_textDevice->device(), ke->nativeVirtualKey(), isPress);
            ei_device_frame(m_textDevice->device(), ei_now(m_ei));
        }
        break;
    }
    default:
        break;
    }
}

void EiConnection::processEisEvents()
{
    while (auto event = ei_get_event(m_ei)) {
        auto cleanup = qScopeGuard([event] {
            ei_event_unref(event);
        });

        const auto eventType = ei_event_get_type(event);
        auto device = ei_event_get_device(event);

        switch (eventType) {
        case EI_EVENT_CONNECT:
            break;
        case EI_EVENT_DISCONNECT:
            qCWarning(KRDP) << "Portal EIS connection disconnected";
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
                m_pointerDevices.push_back(std::make_unique<EisPointerDevice>(device));
            }
            if (!m_keyboardDevice && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
                m_keyboardDevice.reset(new EiDevice(device));
            }
            if (!m_textDevice && ei_device_has_capability(device, EI_DEVICE_CAP_TEXT)) {
                m_textDevice.reset(new EiDevice(device));
            }
            ei_device_start_emulating(device, ei_now(m_ei));
            break;
        case EI_EVENT_DEVICE_REMOVED:
            std::erase_if(m_pointerDevices, [device](const auto &pointerDevice) {
                return pointerDevice->device() == device;
            });
            if (m_keyboardDevice && m_keyboardDevice->device() == device) {
                m_keyboardDevice.reset();
            }
            if (m_textDevice && m_textDevice->device() == device) {
                m_textDevice.reset();
            }
            break;
        case EI_EVENT_DEVICE_RESUMED:
            ei_device_start_emulating(device, ei_now(m_ei));
        default:
            break;
        }
    }
}

void EiConnection::onEisReadyRead()
{
    if (!m_ei) {
        return;
    }

    ei_dispatch(m_ei);
    processEisEvents();
}

}

#include "moc_EiConnection.cpp"
