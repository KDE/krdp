// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.krdpserversettings.private 1.0
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root
    Connections {
        target: kcm
        function onKrdpServerSettingsChanged(): void {
            kcm.writePasswordToWallet(Settings.user, passwordField.text);
        }
        function onPasswordLoaded(user: string, password: string): void {
            if (user === Settings.user) {
                passwordField.text = password;
            }
        }
    }

    ColumnLayout {
        Kirigami.FormLayout {
            id: userLayout
            twinFormLayouts: settingsLayout
            // Users
            Kirigami.Separator {
                id: separator
                Kirigami.FormData.label: i18nc("A list of usernames for KRDP", "Users")
                Kirigami.FormData.isSection: true
            }
            Item {
                Kirigami.FormData.isSection: true
            }
        }

        ColumnLayout {
            Layout.maximumWidth: separator.width - Kirigami.Units.gridUnit * 2
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            QQC2.Label {
                text: i18nc("Guide for adding users to KRDP settings", "Click on an username to modify it or delete it.")
                Kirigami.FormData.isSection: true
            }

            Kirigami.ScrollablePage {
                id: userView
                clip: true

                verticalScrollBarPolicy: QQC2.ScrollBar.AlwaysOn
                Layout.maximumHeight: Kirigami.Units.gridUnit * 8

                Component {
                    id: userComponent
                    QQC2.ItemDelegate {
                        id: itemDelegate
                        text: modelData
                        contentItem: Kirigami.TitleSubtitle {
                            implicitWidth: userListView.width - Kirigami.Units.gridUnit
                            title: itemDelegate.text
                        }

                        onClicked: {
                            console.log(itemDelegate.text, "clicked");
                        }
                    }
                }

                ListView {
                    id: userListView
                    anchors.fill: parent
                    model: Settings.users
                    delegate: userComponent
                    spacing: Kirigami.Units.smallSpacing
                }
            }

            QQC2.Button {
                id: addUserButton
                icon.name: "list-add-user"
                text: i18nc("@action:button", "New User...")
            }
        }

        Kirigami.FormLayout {
            id: settingsLayout
            twinFormLayouts: userLayout
            // Settings
            Kirigami.Separator {
                Kirigami.FormData.label: i18nc("General KRDP related settings", "General Settings")
                Kirigami.FormData.isSection: true
            }
            Item {
                Kirigami.FormData.isSection: true
            }

            QQC2.TextField {
                id: addressField
                Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                Kirigami.FormData.label: i18nc("@label:textbox", "Listen Address:")
                text: Settings.listenAddress
                onTextEdited: {
                    Settings.listenAddress = text;
                }
                KCM.SettingStateBinding {
                    configObject: Settings
                    settingName: "listenAddress"
                }
            }

            QQC2.TextField {
                id: portField
                inputMask: "99999999"
                Layout.maximumWidth: Kirigami.Units.gridUnit * 5
                inputMethodHints: Qt.ImhDigitsOnly
                Kirigami.FormData.label: i18nc("@label:textbox", "Listen Port:")
                text: Settings.listenPort
                onTextEdited: {
                    Settings.listenPort = text;
                }
                KCM.SettingStateBinding {
                    configObject: Settings
                    settingName: "listenPort"
                }
            }

            Item {
                Kirigami.FormData.isSection: true
            }

            RowLayout {
                id: certLayout
                spacing: Kirigami.Units.smallSpacing
                Kirigami.FormData.label: i18nc("@label:textbox", "Certificate path:")
                QQC2.TextField {
                    id: certPathField
                    implicitWidth: Kirigami.Units.gridUnit * 14
                    text: Settings.certificate
                    onTextChanged: {
                        Settings.certificate = text;
                    }
                    KCM.SettingStateBinding {
                        configObject: Settings
                        settingName: "certificate"
                    }
                }
                QQC2.Button {
                    icon.name: "folder-open-symbolic"
                    text: i18nc("@action:button", "Choose Certificate File…")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: {
                        certLoader.key = false;
                        certLoader.active = true;
                    }
                }
            }

            RowLayout {
                id: certKeyLayout
                spacing: Kirigami.Units.smallSpacing
                Kirigami.FormData.label: i18nc("@label:textbox", "Certificate key path:")
                QQC2.TextField {
                    id: certKeyPathField
                    implicitWidth: Kirigami.Units.gridUnit * 14
                    text: Settings.certificateKey
                    onTextChanged: {
                        Settings.certificateKey = text;
                    }
                    KCM.SettingStateBinding {
                        configObject: Settings
                        settingName: "certificateKey"
                    }
                }
                QQC2.Button {
                    icon.name: "folder-open-symbolic"
                    text: i18nc("@action:button", "Choose Certificate Key File…")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: {
                        certLoader.key = true;
                        certLoader.active = true;
                    }
                }
            }

            Item {
                Kirigami.FormData.isSection: true
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
                    }
                }
                QQC2.Slider {
                    id: qualitySlider
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 12
                    from: 0
                    to: 100
                    stepSize: 1
                    Layout.fillWidth: true
                    value: Settings.quality
                    onValueChanged: {
                        Settings.quality = value;
                    }
                    KCM.SettingStateBinding {
                        configObject: Settings
                        settingName: "quality"
                    }
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
}
