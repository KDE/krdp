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
    property Kirigami.OverlaySheet editUserModal: EditUserModal {}
    property Kirigami.OverlaySheet deleteUserModal: DeleteUserModal {}

    Connections {
        target: kcm
        function onKrdpServerSettingsChanged(): void {
        }
    }

    function modifyUser(user: string): void {
        editUserModal.oldUsername = user;
        editUserModal.open();
    }

    function addUser(): void {
        modifyUser("");
    }
    function deleteUser(user: string): void {
        deleteUserModal.selectedUsername = user;
        deleteUserModal.open();
    }

    ColumnLayout {

        // User
        Kirigami.ScrollablePage {
            id: userView
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            clip: true

            verticalScrollBarPolicy: QQC2.ScrollBar.AlwaysOn
            Layout.maximumHeight: root.height / 2

            Component {
                id: userComponent
                QQC2.ItemDelegate {
                    id: itemDelegate
                    text: modelData
                    contentItem: RowLayout {
                        Kirigami.TitleSubtitle {
                            id: editUserButton
                            implicitWidth: userListView.width - deleteUserButton.width - modifyUserButton.width - Kirigami.Units.gridUnit * 1.5
                            title: itemDelegate.text
                        }
                        QQC2.Button {
                            id: modifyUserButton
                            flat: true
                            implicitWidth: Kirigami.Units.gridUnit * 2
                            icon.name: "edit-entry-symbolic"
                            text: i18nc("@label:button", "Modify user...")
                            display: QQC2.AbstractButton.IconOnly
                            onClicked: {
                                root.modifyUser(itemDelegate.text);
                            }
                        }
                        QQC2.Button {
                            id: deleteUserButton
                            flat: true
                            implicitWidth: Kirigami.Units.gridUnit * 2
                            icon.name: "list-remove-user"
                            text: i18nc("@label:button", "Remove user...")
                            display: QQC2.AbstractButton.IconOnly
                            onClicked: {
                                root.deleteUser(itemDelegate.text);
                            }
                        }
                    }
                    onClicked: {
                        root.modifyUser(itemDelegate.text);
                    }
                }
            }

            ListView {
                id: userListView
                anchors.fill: parent
                model: Settings.users
                delegate: userComponent
                spacing: Kirigami.Units.smallSpacing
                headerPositioning: ListView.OverlayHeader
                header: Kirigami.InlineViewHeader {
                    width: userListView.width
                    text: "Usernames"
                    actions: [
                        Kirigami.Action {
                            icon.name: "list-add-user"
                            text: i18nc("@label:button", "Add user...")
                            onTriggered: {
                                root.addUser();
                            }
                        }
                    ]
                }
            }
        }

        // Settings
        Kirigami.FormLayout {
            id: settingsLayout

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

            QQC2.CheckBox {
                id: autoGenCertSwitch
                Kirigami.FormData.label: i18nc("@label:check", "Autogenerate certificates:")
                checked: Settings.autogenerateCertificates
                onCheckedChanged: {
                    Settings.autogenerateCertificates = checked;
                }
                KCM.SettingStateBinding {
                    configObject: Settings
                    settingName: "autogenerateCertificates"
                }
            }

            RowLayout {
                id: certLayout
                enabled: autoGenCertSwitch.checked ? false : true
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
                enabled: autoGenCertSwitch.checked ? false : true
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
