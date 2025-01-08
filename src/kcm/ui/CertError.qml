// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.InlineMessage {
    id: certificateError
    type: Kirigami.MessageType.Error
    position: Kirigami.InlineMessage.Position.Header
    Layout.fillWidth: true
    // TODO better text
    text: i18nc("@info:status", "Generating certificates automatically has failed!")
}
