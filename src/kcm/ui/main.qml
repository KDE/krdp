// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root

    property var settings: kcm.settings()

    EditUserModal {
        id: editUserModal
        parent: root
        // This dialog benefits from being able to stretch with the window; let it
        implicitWidth: Math.max(Kirigami.Units.gridUnit * 15, Math.round(root.width / 2))
    }

    DeleteUserModal {
        id: deleteUserModal
        parent: root
    }

    KeychainErrorDialog {
        id: keychainErrorDialog
        parent: root
    }

    Connections {
        target: kcm
        function onKrdpServerSettingsChanged(): void {
            kcm.toggleAutoconnect(settings.autostart);
        }
        function onGenerateCertificateSucceeded(): void {
            certificateError.visible = false;
        }
        function onGenerateCertificateFailed(): void {
            certificateError.visible = true;
        }
        function onKeychainError(errorText: string): void {
            keychainErrorDialog.errorText = errorText;
            keychainErrorDialog.open();
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

    header: ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            type: Kirigami.MessageType.Error
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.mediumSpacing
            visible: !kcm.isH264Supported()
            text: i18nc("@info:status", "Remote desktop cannot be enabled because your system does not support H264 video encoding. Please contact your distribution regarding how to enable it.")
        }

        Kirigami.InlineMessage {
            id: certificateError
            type: Kirigami.MessageType.Error
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.mediumSpacing
            // TODO better text
            text: i18nc("@info:status", "Generating certificates automatically has failed!")
        }
    }

    ColumnLayout {
        id: contentColumn
        spacing: Kirigami.Units.gridUnit

        QQC2.Label {
            text: i18n("Set up remote login to connect using apps supporting the “RDP” remote desktop protocol.")
            Layout.preferredWidth: userViewFrame.width
            Layout.topMargin: contentColumn.spacing
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
        }

        QQC2.ScrollView {
            id: userViewFrame
            Layout.maximumHeight: Kirigami.Units.gridUnit * 15
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            Layout.preferredWidth: root.width - Kirigami.Units.gridUnit * 5
            Layout.preferredHeight: Kirigami.Units.gridUnit * 10
            enabled: !toggleServerSwitch.checked
            clip: true

            Component.onCompleted: {
                if (background) {
                    background.visible = true;
                }
            }

            Kirigami.PlaceholderMessage {
                width: parent.width - (Kirigami.Units.largeSpacing * 4)
                anchors.centerIn: parent
                anchors.verticalCenterOffset: Kirigami.Units.gridUnit * 2
                visible: userListView.count === 0
                text: i18nc("@info:placeholder", "Add at least one account for remote login")
            }

            contentItem: ListView {
                id: userListView

                headerPositioning: ListView.OverlayHeader
                header: Kirigami.InlineViewHeader {
                    width: userListView.width
                    text: i18nc("@title", "Usernames")
                    actions: [
                        Kirigami.Action {
                            icon.name: "list-add-user"
                            text: i18nc("@label:button", "Add user…")
                            onTriggered: {
                                root.addUser();
                            }
                        }
                    ]
                }

                model: settings.users

                delegate: QQC2.ItemDelegate {
                    id: itemDelegate
                    width: userListView.width
                    text: modelData
                    hoverEnabled: !toggleServerSwitch.checked
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.mediumSpacing

                        QQC2.Label {
                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            text: itemDelegate.text
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        QQC2.Button {
                            id: modifyUserButton
                            icon.name: "edit-entry-symbolic"
                            text: i18nc("@action:button", "Modify user…")
                            display: QQC2.AbstractButton.IconOnly
                            onClicked: {
                                root.modifyUser(itemDelegate.text);
                            }
                            QQC2.ToolTip {
                                text: modifyUserButton.text
                                visible: modifyUserButton.hovered
                                || (Kirigami.Settings.tabletMode && modifyUserButton.pressed)
                            }
                        }

                        QQC2.Button {
                            id: deleteUserButton
                            icon.name: "list-remove-user-symbolic"
                            text: i18nc("@action:button", "Remove user…")
                            display: QQC2.AbstractButton.IconOnly
                            onClicked: {
                                root.deleteUser(itemDelegate.text);
                            }
                            QQC2.ToolTip {
                                text: deleteUserButton.text
                                visible: deleteUserButton.hovered
                                || (Kirigami.Settings.tabletMode && deleteUserButton.pressed)
                            }
                        }
                    }
                    onClicked: {
                        root.modifyUser(itemDelegate.text);
                    }
                }
            }
        }

        // Server toggle
        Kirigami.FormLayout {
            id: toggleLayout
            twinFormLayouts: settingsLayout

            enabled: userListView.count > 0
            QQC2.Switch {
                id: toggleServerSwitch
                checked: kcm.isServerRunning()
                Kirigami.FormData.label: i18nc("@option:check", "Enable RDP server:")
                onCheckedChanged: {
                    kcm.toggleServer(toggleServerSwitch.checked);
                }
            }

            QQC2.CheckBox {
                id: autostartOnLogin
                Kirigami.FormData.label: i18nc("@option:check", "Autostart on login:")
                checked: settings.autostart
                onCheckedChanged: {
                    settings.autostart = checked;
                }
                KCM.SettingStateBinding {
                    configObject: settings
                    settingName: "autostart"
                }
            }
        }

        // Settings
        Kirigami.FormLayout {
            id: settingsLayout
            twinFormLayouts: toggleLayout
            enabled: !toggleServerSwitch.checked && userListView.count > 0

            QQC2.TextField {
                id: addressField
                Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                Kirigami.FormData.label: i18nc("@label:textbox", "Listening Address:")
                text: kcm.listenAddress()
                onTextEdited: {
                    settings.listenAddress = text;
                }
                KCM.SettingStateBinding {
                    configObject: settings
                    settingName: "listenAddress"
                }
            }

            QQC2.TextField {
                id: portField
                inputMask: "99999999"
                Layout.maximumWidth: Kirigami.Units.gridUnit * 5
                inputMethodHints: Qt.ImhDigitsOnly
                Kirigami.FormData.label: i18nc("@label:textbox", "Listening Port:")
                text: settings.listenPort
                onTextEdited: {
                    settings.listenPort = text;
                }
                KCM.SettingStateBinding {
                    configObject: settings
                    settingName: "listenPort"
                }
            }

            Item {
                Kirigami.FormData.isSection: true
            }

            QQC2.CheckBox {
                id: autoGenCertSwitch
                Kirigami.FormData.label: i18nc("@label:check", "Autogenerate certificates:")
                checked: settings.autogenerateCertificates
                onCheckedChanged: {
                    settings.autogenerateCertificates = checked;
                    if (checked) {
                        kcm.generateCertificate();
                    }
                }
                KCM.SettingStateBinding {
                    configObject: settings
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
                    text: settings.certificate
                    onTextChanged: {
                        settings.certificate = text;
                    }
                    KCM.SettingStateBinding {
                        configObject: settings
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
                    text: settings.certificateKey
                    onTextChanged: {
                        settings.certificateKey = text;
                    }
                    KCM.SettingStateBinding {
                        configObject: settings
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
            ColumnLayout {
                enabled: !toggleServerSwitch.checked && userListView.count > 0
                Layout.preferredWidth: certKeyLayout.width

                Kirigami.FormData.label: i18nc("@label:textbox", "Video quality:")
                Kirigami.FormData.buddyFor: qualitySlider
                QQC2.Slider {
                    id: qualitySlider
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    value: settings.quality
                    onValueChanged: {
                        settings.quality = value;
                    }
                    KCM.SettingStateBinding {
                        configObject: settings
                        settingName: "quality"
                    }
                }
                RowLayout {
                    QQC2.Label {
                        text: i18nc("@label:slider", "Responsiveness")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        text: i18nc("@label:slider", "Quality")
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
