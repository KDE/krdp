// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PlasmaScreencastV1Session.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QQueue>
#include <QWaylandClientExtensionTemplate>
#include <qpa/qplatformnativeinterface.h>

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <xkbcommon/xkbcommon.h>

#include "qwayland-fake-input.h"
#include "qwayland-wayland.h"
#include "screencasting_p.h"

#include "VideoStream.h"
#include "krdp_logging.h"

namespace KRdp
{

class FakeInput : public QWaylandClientExtensionTemplate<FakeInput>, public QtWayland::org_kde_kwin_fake_input
{
public:
    FakeInput()
        : QWaylandClientExtensionTemplate<FakeInput>(4)
    {
        initialize();
        Q_ASSERT(isActive());
    }
};

namespace
{
struct XKBStateDeleter {
    void operator()(struct xkb_state *state) const
    {
        xkb_state_unref(state);
    }
};
struct XKBKeymapDeleter {
    void operator()(struct xkb_keymap *keymap) const
    {
        xkb_keymap_unref(keymap);
    }
};
struct XKBContextDeleter {
    void operator()(struct xkb_context *context) const
    {
        xkb_context_unref(context);
    }
};
using ScopedXKBState = std::unique_ptr<struct xkb_state, XKBStateDeleter>;
using ScopedXKBKeymap = std::unique_ptr<struct xkb_keymap, XKBKeymapDeleter>;
using ScopedXKBContext = std::unique_ptr<struct xkb_context, XKBContextDeleter>;
}
class Xkb : public QtWayland::wl_keyboard
{
public:
    struct Code {
        const uint32_t level;
        const uint32_t code;
    };
    std::optional<Code> keycodeFromKeysym(xkb_keysym_t keysym)
    {
        /* The offset between KEY_* numbering, and keycodes in the XKB evdev
         * dataset. */
        static const uint EVDEV_OFFSET = 8;

        auto layout = xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_EFFECTIVE);
        const xkb_keycode_t max = xkb_keymap_max_keycode(m_keymap.get());
        for (xkb_keycode_t keycode = xkb_keymap_min_keycode(m_keymap.get()); keycode < max; keycode++) {
            uint levelCount = xkb_keymap_num_levels_for_key(m_keymap.get(), keycode, layout);
            for (uint currentLevel = 0; currentLevel < levelCount; currentLevel++) {
                const xkb_keysym_t *syms;
                uint num_syms = xkb_keymap_key_get_syms_by_level(m_keymap.get(), keycode, layout, currentLevel, &syms);
                for (uint sym = 0; sym < num_syms; sym++) {
                    if (syms[sym] == keysym) {
                        return Code{currentLevel, keycode - EVDEV_OFFSET};
                    }
                }
            }
        }
        return {};
    }

    static Xkb *self()
    {
        static Xkb self;
        return &self;
    }

private:
    Xkb()
    {
        m_ctx.reset(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
        if (!m_ctx) {
            qCWarning(KRDP) << "Failed to create xkb context";
            return;
        }
        m_keymap.reset(xkb_keymap_new_from_names(m_ctx.get(), nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS));
        if (!m_keymap) {
            qCWarning(KRDP) << "Failed to create the keymap";
            return;
        }
        m_state.reset(xkb_state_new(m_keymap.get()));
        if (!m_state) {
            qCWarning(KRDP) << "Failed to create the xkb state";
            return;
        }

        QPlatformNativeInterface *nativeInterface = qGuiApp->platformNativeInterface();
        auto seat = static_cast<wl_seat *>(nativeInterface->nativeResourceForIntegration("wl_seat"));
        init(wl_seat_get_keyboard(seat));
    }

    void keyboard_keymap(uint32_t format, int32_t fd, uint32_t size) override
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            qCWarning(KRDP) << "unknown keymap format:" << format;
            close(fd);
            return;
        }

        char *map_str = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
        if (map_str == MAP_FAILED) {
            close(fd);
            return;
        }

        m_keymap.reset(xkb_keymap_new_from_string(m_ctx.get(), map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
        munmap(map_str, size);
        close(fd);

        if (m_keymap)
            m_state.reset(xkb_state_new(m_keymap.get()));
        else
            m_state.reset(nullptr);
    }

    ScopedXKBContext m_ctx;
    ScopedXKBKeymap m_keymap;
    ScopedXKBState m_state;
};

class KRDP_NO_EXPORT PlasmaScreencastV1Session::Private
{
public:
    Server *server = nullptr;

    Screencasting m_screencasting;
    ScreencastingStream *request = nullptr;
    FakeInput *remoteInterface = nullptr;
};

PlasmaScreencastV1Session::PlasmaScreencastV1Session()
    : AbstractSession()
    , d(std::make_unique<Private>())
{
    d->remoteInterface = new FakeInput();
}

PlasmaScreencastV1Session::~PlasmaScreencastV1Session()
{
    qCDebug(KRDP) << "Closing Plasma Remote Session";
}

void PlasmaScreencastV1Session::start()
{
    if (auto vm = virtualMonitor()) {
        d->request = d->m_screencasting.createVirtualMonitorStream(vm->name, vm->size, vm->dpr, Screencasting::Metadata);
    } else if (!activeStream()) {
        d->request = d->m_screencasting.createWorkspaceStream(Screencasting::Metadata);
    }
    connect(d->request, &ScreencastingStream::failed, this, &PlasmaScreencastV1Session::error);
    connect(d->request, &ScreencastingStream::created, this, [this](uint nodeId) {
        qCDebug(KRDP) << "Started Plasma session";

        setLogicalSize(d->request->size());
        auto encodedStream = stream();
        encodedStream->setNodeId(nodeId);
        encodedStream->setEncodingPreference(PipeWireBaseEncodedStream::EncodingPreference::Speed);
        encodedStream->setEncoder(PipeWireEncodedStream::H264Baseline);
        connect(encodedStream, &PipeWireEncodedStream::newPacket, this, &PlasmaScreencastV1Session::onPacketReceived);
        connect(encodedStream, &PipeWireEncodedStream::sizeChanged, this, &PlasmaScreencastV1Session::setSize);
        connect(encodedStream, &PipeWireEncodedStream::cursorChanged, this, &PlasmaScreencastV1Session::cursorUpdate);
        setStarted(true);
    });
}

void PlasmaScreencastV1Session::sendEvent(const std::shared_ptr<QEvent> &event)
{
    auto encodedStream = stream();
    if (!encodedStream || !encodedStream->isActive()) {
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
        } else {
            qCWarning(KRDP) << "Unsupported mouse button" << me->button();
            return;
        }
        uint state = me->type() == QEvent::MouseButtonPress ? 1 : 0;
        d->remoteInterface->button(button, state);
        break;
    }
    case QEvent::MouseMove: {
        auto me = std::static_pointer_cast<QMouseEvent>(event);
        auto position = me->position();
        auto logicalPosition = QPointF{(position.x() / size().width()) * logicalSize().width(), (position.y() / size().height()) * logicalSize().height()};
        d->remoteInterface->pointer_motion_absolute(logicalPosition.x(), logicalPosition.y());
        break;
    }
    case QEvent::Wheel: {
        auto we = std::static_pointer_cast<QWheelEvent>(event);
        auto delta = we->angleDelta();
        if (delta.y() != 0) {
            d->remoteInterface->axis(WL_POINTER_AXIS_VERTICAL_SCROLL, delta.y() / 120);
        }
        if (delta.x() != 0) {
            d->remoteInterface->axis(WL_POINTER_AXIS_HORIZONTAL_SCROLL, delta.x() / 120);
        }
        break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto ke = std::static_pointer_cast<QKeyEvent>(event);
        auto state = ke->type() == QEvent::KeyPress ? 1 : 0;

        if (ke->nativeScanCode()) {
            d->remoteInterface->keyboard_key(ke->nativeScanCode(), state);
        } else {
            auto keycode = Xkb::self()->keycodeFromKeysym(ke->nativeVirtualKey());
            if (!keycode) {
                qCWarning(KRDP) << "Failed to convert keysym into keycode" << ke->nativeVirtualKey();
                return;
            }

            auto sendKey = [this, state](int keycode) {
                d->remoteInterface->keyboard_key(keycode, state);
            };
            switch (keycode->level) {
            case 0:
                break;
            case 1:
                sendKey(KEY_LEFTSHIFT);
                break;
            case 2:
                sendKey(KEY_RIGHTALT);
                break;
            default:
                qCWarning(KRDP) << "Unsupported key level" << keycode->level;
                break;
            }
            sendKey(keycode->code);
        }
        break;
    }
    default:
        break;
    }
}

void PlasmaScreencastV1Session::setClipboardData(std::unique_ptr<QMimeData> data)
{
    Q_UNUSED(data);
}

void PlasmaScreencastV1Session::onPacketReceived(const PipeWireEncodedStream::Packet &data)
{
    VideoFrame frameData;

    frameData.size = size();
    frameData.data = data.data();
    frameData.isKeyFrame = data.isKeyFrame();

    Q_EMIT frameReceived(frameData);
}

}
