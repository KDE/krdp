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

class Session;

class KRDP_EXPORT InputHandler : public QObject
{
    Q_OBJECT

public:
    InputHandler(Session *session);
    ~InputHandler() override;

    void initialize(rdpInput *input);

    Q_SIGNAL void inputEvent(QInputEvent *event);

private:
    friend BOOL inputSynchronizeEvent(rdpInput *, uint32_t);
    friend BOOL inputMouseEvent(rdpInput *, uint16_t, uint16_t, uint16_t);
    friend BOOL inputExtendedMouseEvent(rdpInput *, uint16_t, uint16_t, uint16_t);
#ifdef FREERDP3
    friend BOOL inputKeyboardEvent(rdpInput *, uint16_t, uint8_t);
#else
    friend BOOL inputKeyboardEvent(rdpInput *, uint16_t, uint16_t);
#endif
    friend BOOL inputUnicodeKeyboardEvent(rdpInput *, uint16_t, uint16_t);

    bool synchronizeEvent(uint32_t flags);
    bool mouseEvent(uint16_t x, uint16_t y, uint16_t flags);
    bool extendedMouseEvent(uint16_t x, uint16_t y, uint16_t flags);
    bool keyboardEvent(uint16_t code, uint16_t flags);
    bool unicodeKeyboardEvent(uint16_t code, uint16_t flags);

    class Private;
    const std::unique_ptr<Private> d;
};

}
