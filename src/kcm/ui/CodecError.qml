// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.InlineMessage {
    type: Kirigami.MessageType.Error
    position: Kirigami.InlineMessage.Position.Header
    Layout.fillWidth: true
    visible: !kcm.isH264Supported()
    text: i18nc("@info:status", "Remote desktop cannot be enabled because your system does not support H264 video encoding. Please contact your distribution regarding how to enable it.")
}
