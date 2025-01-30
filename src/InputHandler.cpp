// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "InputHandler.h"

#include <QKeyEvent>

#include <xkbcommon/xkbcommon.h>

#include "PeerContext_p.h"

#include "krdp_logging.h"

namespace KRdp
{

BOOL inputSynchronizeEvent(rdpInput *input, uint32_t flags)
{
    auto context = reinterpret_cast<PeerContext *>(input->context);

    if (context->inputHandler->synchronizeEvent(flags)) {
        return TRUE;
    }

    return FALSE;
}

BOOL inputMouseEvent(rdpInput *input, uint16_t flags, uint16_t x, uint16_t y)
{
    auto context = reinterpret_cast<PeerContext *>(input->context);

    if (context->inputHandler->mouseEvent(x, y, flags)) {
        return TRUE;
    }

    return FALSE;
}

BOOL inputExtendedMouseEvent(rdpInput *input, uint16_t flags, uint16_t x, uint16_t y)
{
    auto context = reinterpret_cast<PeerContext *>(input->context);

    if (context->inputHandler->extendedMouseEvent(x, y, flags)) {
        return TRUE;
    }

    return FALSE;
}

BOOL inputKeyboardEvent(rdpInput *input, uint16_t flags, uint8_t code)
{
    auto context = reinterpret_cast<PeerContext *>(input->context);

    if (context->inputHandler->keyboardEvent(code, flags)) {
        return TRUE;
    }

    return FALSE;
}

BOOL inputUnicodeKeyboardEvent(rdpInput *input, uint16_t flags, uint16_t code)
{
    auto context = reinterpret_cast<PeerContext *>(input->context);

    if (context->inputHandler->unicodeKeyboardEvent(code, flags)) {
        return TRUE;
    }

    return FALSE;
}

class KRDP_NO_EXPORT InputHandler::Private
{
public:
    RdpConnection *session;
    rdpInput *input;
};

InputHandler::InputHandler(KRdp::RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
}

InputHandler::~InputHandler() noexcept
{
}

void InputHandler::initialize(rdpInput *input)
{
    d->input = input;
    input->SynchronizeEvent = inputSynchronizeEvent;
    input->MouseEvent = inputMouseEvent;
    input->ExtendedMouseEvent = inputExtendedMouseEvent;
    input->KeyboardEvent = inputKeyboardEvent;
    input->UnicodeKeyboardEvent = inputUnicodeKeyboardEvent;
}

bool InputHandler::synchronizeEvent(uint32_t /*flags*/)
{
    // TODO: This syncs caps/num/scroll lock keys, do we actually want to?
    return true;
}

bool InputHandler::mouseEvent(uint16_t x, uint16_t y, uint16_t flags)
{
    QPointF position = QPointF(x, y);

    Qt::MouseButton button = Qt::NoButton;
    if (flags & PTR_FLAGS_BUTTON1) {
        button = Qt::LeftButton;
    } else if (flags & PTR_FLAGS_BUTTON2) {
        button = Qt::RightButton;
    } else if (flags & PTR_FLAGS_BUTTON3) {
        button = Qt::MiddleButton;
    }

    if (flags & PTR_FLAGS_WHEEL || flags & PTR_FLAGS_WHEEL_NEGATIVE) {
        auto axis = flags & WheelRotationMask;
        if (axis & PTR_FLAGS_WHEEL_NEGATIVE) {
            axis = (~axis & WheelRotationMask) + 1;
        }
        axis *= flags & PTR_FLAGS_WHEEL_NEGATIVE ? 1 : -1;
        auto event =
            std::make_shared<QWheelEvent>(QPointF{}, position, QPoint{}, QPoint{0, axis}, Qt::NoButton, Qt::KeyboardModifiers{}, Qt::NoScrollPhase, false);
        Q_EMIT inputEvent(event);
        return true;
    }

    std::shared_ptr<QMouseEvent> event;
    if (flags & PTR_FLAGS_DOWN) {
        event = std::make_shared<QMouseEvent>(QEvent::MouseButtonPress, QPointF{}, position, button, button, Qt::NoModifier);
    } else if (flags & PTR_FLAGS_MOVE) {
        event = std::make_shared<QMouseEvent>(QEvent::MouseMove, QPointF{}, position, button, button, Qt::NoModifier);
    } else {
        event = std::make_shared<QMouseEvent>(QEvent::MouseButtonRelease, QPointF{}, position, button, button, Qt::NoModifier);
    }
    Q_EMIT inputEvent(event);

    return true;
}

bool InputHandler::extendedMouseEvent(uint16_t x, uint16_t y, uint16_t flags)
{
    if (flags & PTR_FLAGS_MOVE) {
        return mouseEvent(x, y, PTR_FLAGS_MOVE);
    }

    Qt::MouseButton button = Qt::NoButton;
    if (flags & PTR_XFLAGS_BUTTON1) {
        button = Qt::BackButton;
    } else if (flags & PTR_XFLAGS_BUTTON2) {
        button = Qt::ForwardButton;
    } else {
        return false;
    }

    std::shared_ptr<QMouseEvent> event;
    if (flags & PTR_XFLAGS_DOWN) {
        event = std::make_shared<QMouseEvent>(QEvent::MouseButtonPress, QPointF{}, QPointF(x, y), button, button, Qt::KeyboardModifiers{});

    } else {
        event = std::make_shared<QMouseEvent>(QEvent::MouseButtonRelease, QPointF{}, QPointF(x, y), button, button, Qt::KeyboardModifiers{});
    }
    Q_EMIT inputEvent(event);

    return true;
}

bool InputHandler::keyboardEvent(uint16_t code, uint16_t flags)
{
    auto virtualCode = GetVirtualKeyCodeFromVirtualScanCode(flags & KBD_FLAGS_EXTENDED ? code | KBDEXT : code, 4);
    virtualCode = flags & KBD_FLAGS_EXTENDED ? virtualCode | KBDEXT : virtualCode;

    quint32 keycode = GetKeycodeFromVirtualKeyCode(virtualCode, WINPR_KEYCODE_TYPE_EVDEV);

    auto type = flags & KBD_FLAGS_RELEASE ? QEvent::KeyRelease : QEvent::KeyPress;

    auto event = std::make_shared<QKeyEvent>(type, 0, Qt::KeyboardModifiers{}, keycode, 0, 0);
    Q_EMIT inputEvent(event);

    return true;
}

bool InputHandler::unicodeKeyboardEvent(uint16_t code, uint16_t flags)
{
    auto text = QString(QChar::fromUcs2(code));
    auto keysym = xkb_utf32_to_keysym(text.toUcs4().first());
    if (!keysym) {
        return true;
    }

    auto type = flags & KBD_FLAGS_RELEASE ? QEvent::KeyRelease : QEvent::KeyPress;

    auto event = std::make_shared<QKeyEvent>(type, 0, Qt::KeyboardModifiers{}, 0, keysym, 0);
    Q_EMIT inputEvent(event);

    return true;
}

}

#include "moc_InputHandler.cpp"
