// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtCore
import QtQuick

import QtQuick.Dialogs as QtDialogs

Loader {
    id: certLoader

    property bool selectKey

    active: false
    sourceComponent: QtDialogs.FileDialog {
        id: fileDialog

        title: selectKey ? i18nc("@title:window", "Select certificate key file") : i18nc("@title:window", "Select certificate file")
        Component.onCompleted: open()

        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
        nameFilters: selectKey ? ["Certificate keys (*.key)"] : ["Certificates (*.crt)"]

        onAccepted: {
            var file = kcm.toLocalFile(selectedFile);
            if (selectKey) {
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
