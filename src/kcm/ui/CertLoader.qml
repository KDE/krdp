// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs

Loader {
    id: certLoader
    property bool key
    active: false
    sourceComponent: QtDialogs.FileDialog {
        id: fileDialog
        title: key ? i18nc("@title:window", "Select Certificate Key file") : i18nc("@title:window", "Select Certificate file")
        Component.onCompleted: open()
        onAccepted: {
            var file = kcm.toLocalFile(selectedFile);
            if (key) {
                certKeyPathField.text = file;
            } else {
                certPathField.text = file;
            }
            certLoader.active = false;
        }
        onRejected: {
            certLoader.active = false;
        }
    }
}
