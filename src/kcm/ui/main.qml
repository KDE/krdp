// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCMUtils

KCMUtils.SimpleKCM {
    id: root
    title: "KRDP Configuration"

    Kirigami.FormLayout {
        id: layout
        Layout.fillWidth: true

        QQC2.TextField {
            Kirigami.FormData.label: "Username:"
        }
        QQC2.TextField {
            echoMode: TextInput.Password
            Kirigami.FormData.label: "Password:"
        }
        QQC2.TextField {
            inputMask: "99999999"
            inputMethodHints: Qt.ImhDigitsOnly
            Kirigami.FormData.label: "Port:"
        }
        QQC2.Button {
            text: qsTr("Certificate")
            Layout.fillWidth: true
            onClicked: {
                certLoader.key = false;
                certLoader.active = true;
            }
        }
        QQC2.Button {
            text: qsTr("Certificate key")
            Layout.fillWidth: true
            onClicked: {
                certLoader.key = true;
                certLoader.active = true;
            }
        }
        QQC2.SpinBox {
            Kirigami.FormData.label: "Quality:"
            value: 100
            from: 0
            to: 100
        }
        QQC2.Switch {
            text: qsTr("Use Plasma protocols?")
            checked: false
        }
    }

    Loader {
        id: certLoader
        property bool key
        active: false
        sourceComponent: QtDialogs.FileDialog {
            title: i18n("Select Certificate file")
            currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
            Component.onCompleted: open()
            onAccepted: {
                kcm.installCertificateFromFile(selectedFile, key);
                certLoader.active = false;
            }
            onRejected: {
                certLoader.active = false;
            }
        }
    }
}
