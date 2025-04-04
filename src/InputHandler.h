// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>

#include <QInputEvent>
#include <QObject>

#include <freerdp/freerdp.h>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

/**
 * This class processes RDP input events and converts them to Qt events.
 *
 * One input handler is created per session.
 */
class KRDP_EXPORT InputHandler : public QObject
{
    Q_OBJECT

public:
    explicit InputHandler(RdpConnection *session);
    ~InputHandler() override;

    /**
     * Initialize the InputHandler. Called from within Session::initialize().
     */
    void initialize(rdpInput *input);

    /**
     * Emitted whenever a new input event was received from the client.
     *
     * \param event The input event that was received.
     */
    // Note: Intentional pass-by-value to ensure lifetime of the shared_ptr is extended.
    Q_SIGNAL void inputEvent(std::shared_ptr<QInputEvent> event);

private:
    // FreeRDP callbacks that need to call the event handler functions in the
    // handler.
    friend BOOL inputSynchronizeEvent(rdpInput *, uint32_t);
    friend BOOL inputMouseEvent(rdpInput *, uint16_t, uint16_t, uint16_t);
    friend BOOL inputExtendedMouseEvent(rdpInput *, uint16_t, uint16_t, uint16_t);
    friend BOOL inputKeyboardEvent(rdpInput *, uint16_t, uint8_t);
    friend BOOL inputUnicodeKeyboardEvent(rdpInput *, uint16_t, uint16_t);

    /**
     * Called when the state of modifier keys changes.
     *
     * \param flags The state of modifier keys.
     */
    bool synchronizeEvent(uint32_t flags);
    /**
     * Called when a new mouse event was sent.
     *
     * \param x The X position, in client coordinates.
     * \param y The Y position, in client coordinates.
     * \param flags Mouse button state and other flags.
     */
    bool mouseEvent(uint16_t x, uint16_t y, uint16_t flags);
    bool extendedMouseEvent(uint16_t x, uint16_t y, uint16_t flags);
    bool keyboardEvent(uint16_t code, uint16_t flags);
    bool unicodeKeyboardEvent(uint16_t code, uint16_t flags);

    class Private;
    const std::unique_ptr<Private> d;
};

}
