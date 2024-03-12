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
            id: usernameField
            Kirigami.FormData.label: "Username:"
            text: kcm.username
            onTextEdited: {
                kcm.username = text;
                kcm.needsSave = true;
            }
        }

        QQC2.TextField {
            id: passwordField
            echoMode: TextInput.Password
            Kirigami.FormData.label: "Password:"
            text: kcm.password
            onTextEdited: {
                kcm.password = text;
                kcm.needsSave = true;
            }
        }

        QQC2.TextField {
            id: portField
            inputMask: "99999999"
            inputMethodHints: Qt.ImhDigitsOnly
            Kirigami.FormData.label: "Port:"
            text: kcm.port
            onTextEdited: {
                kcm.port = parseInt(text);
                kcm.needsSave = true;
            }
        }

        RowLayout {
            id: certLayout
            Kirigami.FormData.label: "Certificate path:"
            Layout.fillWidth: true
            QQC2.TextField {
                id: certPathField
                text: kcm.certPath
                onTextEdited: {
                    kcm.certPath = text;
                    kcm.needsSave = true;
                }
            }
            QQC2.Button {
                text: qsTr("Browse...")
                Layout.maximumWidth: Kirigami.Units.gridUnit * 2
                onClicked: {
                    certLoader.key = false;
                    certLoader.active = true;
                }
            }
        }

        RowLayout {
            id: certKeyLayout
            Kirigami.FormData.label: "Certificate key path:"
            QQC2.TextField {
                id: certKeyPathField
                text: kcm.certKeyPath
                onTextEdited: {
                    kcm.certKeyPath = text;
                    kcm.needsSave = true;
                }
            }
            QQC2.Button {
                text: qsTr("Browse...")
                Layout.maximumWidth: Kirigami.Units.gridUnit * 2
                onClicked: {
                    certLoader.key = true;
                    certLoader.active = true;
                }
            }
        }

        RowLayout {
            Kirigami.FormData.label: "Quality:"
            QQC2.TextField {
                inputMethodHints: Qt.ImhDigitsOnly
                text: qualitySlider.value
                Layout.fillWidth: false
                Layout.maximumWidth: Kirigami.Units.gridUnit * 2
                onTextEdited: {
                    qualitySlider.value = text;
                    kcm.quality = text;
                    kcm.needsSave = true;
                }
            }
            QQC2.Label {
                text: "Speed"
                Layout.fillWidth: false
            }
            QQC2.Slider {
                id: qualitySlider
                from: 0
                to: 100
                value: kcm.quality
                stepSize: 1
                Layout.fillWidth: true
                onMoved: {
                    kcm.quality = value;
                    kcm.needsSave = true;
                }
            }
            QQC2.Label {
                text: "Quality"
                Layout.fillWidth: false
            }
        }
    }

    Loader {
        id: certLoader
        property bool key
        active: false
        sourceComponent: QtDialogs.FileDialog {
            title: i18n("Select Certificate file")
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
