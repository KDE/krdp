// SPDX-FileCopyrightText: 2025 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.PromptDialog {
    id: startupFailureDialog

    property string errorText

    showCloseButton: false
    title: i18nc("@title:window", "Startup Failure")

    dialogType: Kirigami.PromptDialog.Error

    ColumnLayout {
        QQC2.Label {
            Layout.fillWidth: true
            text: i18nc("@info", "The RDP server failed to start.")
        }
        QQC2.TextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: startupFailureDialog.errorText
            readOnly: true
        }
    }

    standardButtons: Kirigami.Dialog.Ok

    onAccepted: {
        errorText = "";
    }
}
