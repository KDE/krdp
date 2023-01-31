// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "InputHandler.h"

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
    Session *session;
    rdpInput *input;
};

InputHandler::InputHandler(KRdp::Session *session)
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

bool InputHandler::synchronizeEvent(uint32_t flags)
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__ << flags;
    return true;
}

bool InputHandler::mouseEvent(uint16_t x, uint16_t y, uint16_t flags)
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__ << x << y << flags;
    return true;
}

bool InputHandler::extendedMouseEvent(uint16_t x, uint16_t y, uint16_t flags)
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__ << x << y << flags;
    return true;
}

bool InputHandler::keyboardEvent(uint8_t code, uint16_t flags)
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__ << code << flags;
    return true;
}

bool InputHandler::unicodeKeyboardEvent(uint16_t code, uint16_t flags)
{
    qCDebug(KRDP) << __PRETTY_FUNCTION__ << code << flags;
    return true;
}

}
