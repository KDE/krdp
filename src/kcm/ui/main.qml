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
    leftPadding: 0
    rightPadding: 0
    topPadding: 0

    property var settings: kcm.settings()

    EditUserModal {
        id: editUserModal
        parent: root
        width: Kirigami.Units.gridUnit * 15
        height: Kirigami.Units.gridUnit * 10
        y: root.height / 3
    }

    DeleteUserModal {
        id: deleteUserModal
        parent: root
        width: Kirigami.Units.gridUnit * 15
        height: Kirigami.Units.gridUnit * 10
        y: root.height / 3
    }

    KeychainErrorDialog {
        id: keychainErrorDialog
        parent: root
        width: Kirigami.Units.gridUnit * 15
        height: Kirigami.Units.gridUnit * 10
        y: root.height / 3
    }

    Connections {
        target: kcm
        function onKrdpServerSettingsChanged(): void {
            kcm.toggleAutoconnect(settings.autostart);
        }
        function onGenerateCertificateSucceeded(): void {
            certificateError.enabled = false;
        }
        function onGenerateCertificateFailed(): void {
            certificateError.enabled = true;
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

    ColumnLayout {
        Item {
            id: encodingError
            implicitWidth: root.width - Kirigami.Units.gridUnit
            implicitHeight: Kirigami.Units.gridUnit * 3
            enabled: !kcm.isH264Supported()
            visible: enabled
            Kirigami.InlineMessage {
                type: Kirigami.MessageType.Error
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                visible: parent.enabled
                text: i18nc("@info:status", "Remote desktop cannot be enabled because your system does not support H264 video encoding. Please contact your distribution regarding how to enable it.")
            }
        }

        Item {
            id: certificateError
            implicitWidth: root.width - Kirigami.Units.gridUnit
            implicitHeight: Kirigami.Units.gridUnit * 3
            enabled: false
            visible: enabled
            Kirigami.InlineMessage {
                type: Kirigami.MessageType.Error
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                visible: parent.enabled
                // TODO better text
                text: i18nc("@info:status", "Generating certificates automatically has failed!")
            }
        }

        QQC2.Label {
            Layout.topMargin: Kirigami.Units.gridUnit
            text: i18n("Set up remote login to connect using apps supporting the “RDP” remote desktop protocol.")
            Layout.preferredWidth: userViewFrame.width - Kirigami.Units.gridUnit * 2
            padding: Kirigami.Units.gridUnit
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
        }

        // User
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
                        text: i18nc("@label:button", "Modify user…")
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
                        text: i18nc("@label:button", "Remove user…")
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

            contentItem: ListView {
                id: userListView
                model: settings.users
                delegate: userComponent

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
            }

            Kirigami.PlaceholderMessage {
                width: parent.width - (Kirigami.Units.largeSpacing * 4)
                anchors.centerIn: parent
                anchors.verticalCenterOffset: Kirigami.Units.gridUnit * 2
                visible: userListView.count === 0
                text: i18nc("@info:placeholder", "Add at least one account for remote login")
            }
        }

        // Server toggle
        Kirigami.FormLayout {
            Layout.topMargin: Kirigami.Units.gridUnit
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
            Item {
                Kirigami.FormData.isSection: true
            }

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
