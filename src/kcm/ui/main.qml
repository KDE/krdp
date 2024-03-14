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
    title: "Remote Desktop"
    Layout.fillWidth: true

    Kirigami.FormLayout {
        id: layout

        QQC2.TextField {
            id: usernameField
            Layout.maximumWidth: Kirigami.Units.gridUnit * 8
            Kirigami.FormData.label: i18nc("@label:textbox", "Username:")
            text: kcm.username
            onTextEdited: {
                kcm.username = text;
                kcm.needsSave = true;
            }
        }

        Kirigami.PasswordField {
            id: passwordField
            Layout.maximumWidth: Kirigami.Units.gridUnit * 8
            echoMode: TextInput.Password
            Kirigami.FormData.label: i18nc("@label:textbox", "Password:")
            text: kcm.password
            onTextEdited: {
                kcm.password = text;
                kcm.needsSave = true;
            }
        }

        QQC2.TextField {
            id: portField
            inputMask: "99999999"
            Layout.maximumWidth: Kirigami.Units.gridUnit * 5
            inputMethodHints: Qt.ImhDigitsOnly
            Kirigami.FormData.label: i18nc("@label:textbox", "Port:")
            text: kcm.port
            onTextEdited: {
                kcm.port = parseInt(text);
                kcm.needsSave = true;
            }
        }

        Item {
            Layout.fillWidth: true
        }

        RowLayout {
            id: certLayout
            width: usernameField.width
            Kirigami.FormData.label: i18nc("@label:textbox", "Certificate path:")
            QQC2.TextField {
                id: certPathField
                implicitWidth: Kirigami.Units.gridUnit * 14
                text: kcm.certPath
                onTextEdited: {
                    kcm.certPath = text;
                    kcm.needsSave = true;
                }
            }
            QQC2.Button {
                icon.name: "folder-open-symbolic"
                text: i18nc("@label:chooser", "Open file picker for Certificate file")
                display: QQC2.AbstractButton.IconOnly
                onClicked: {
                    certLoader.key = false;
                    certLoader.active = true;
                }
            }
        }

        RowLayout {
            id: certKeyLayout
            width: usernameField.width
            Kirigami.FormData.label: i18nc("@label:textbox", "Certificate key path:")
            QQC2.TextField {
                id: certKeyPathField
                implicitWidth: Kirigami.Units.gridUnit * 14
                text: kcm.certKeyPath
                onTextEdited: {
                    kcm.certKeyPath = text;
                    kcm.needsSave = true;
                }
            }
            QQC2.Button {
                icon.name: "folder-open-symbolic"
                text: i18nc("@label:chooser", "Open file picker for Certificate Key file")
                display: QQC2.AbstractButton.IconOnly
                onClicked: {
                    certLoader.key = true;
                    certLoader.active = true;
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }

        RowLayout {
            Kirigami.FormData.label: i18nc("@label:slider", "Quality:")
            QQC2.SpinBox {
                inputMethodHints: Qt.ImhDigitsOnly
                from: 0
                to: 100
                stepSize: 1
                value: qualitySlider.value
                onValueModified: {
                    qualitySlider.value = value;
                    kcm.quality = value;
                    kcm.needsSave = true;
                }
            }
            QQC2.Slider {
                id: qualitySlider
                Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                from: 0
                to: 100
                stepSize: 1
                value: kcm.quality
                Layout.fillWidth: true
                onMoved: {
                    kcm.quality = value;
                    kcm.needsSave = true;
                }
            }
        }
    }

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
                    kcm.certKeyPath = file;
                    certKeyPathField.text = file;
                } else {
                    kcm.certPath = file;
                    certPathField.text = file;
                }
                kcm.needsSave = true;
                certLoader.active = false;
            }
            onRejected: {
                certLoader.active = false;
            }
        }
    }
}
