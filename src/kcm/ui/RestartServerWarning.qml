// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.InlineMessage {
    type: Kirigami.MessageType.Warning
    position: Kirigami.InlineMessage.Position.Header
    Layout.fillWidth: true
    visible: false
    text: i18nc("@info:status", "Restart the server to apply changed settings. This may disconnect active connections.")
    actions: [
        Kirigami.Action {
            icon.name: "system-reboot-symbolic"
            text: i18n("Restart Server")
            onTriggered: source => {
                kcm.restartServer();
                restartServerWarning.visible = false;
            }
        }
    ]
}
