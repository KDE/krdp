// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.ScrollViewKCM {
    id: root

    property var settings: kcm.settings()

    extraFooterTopPadding: true // This makes separator below scrollview visible

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
        function onServerRunning(isServerRunning: bool): void {
            toggleServerSwitch.checked = isServerRunning;
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

    actions: [
        Kirigami.Action {
            id: toggleServerSwitch
            text: i18nc("@option:check Enable RDP server", "Enable RDP server")
            checkable: true
            Component.onCompleted: {
                kcm.checkServerRunning();
            }
            onTriggered: source => {
                kcm.toggleServer(source.checked);
            }
            displayComponent: QQC2.Switch {
                action: toggleServerSwitch
            }
        }
    ]

    headerPaddingEnabled: false // Let the InlineMessages touch the edges
    header: ColumnLayout {
        id: headerLayout
        readonly property int spacings: Kirigami.Units.largeSpacing
        spacing: 0
        Layout.margins: spacings

        Kirigami.InlineMessage {
            type: Kirigami.MessageType.Error
            position: Kirigami.InlineMessage.Position.Header
            Layout.fillWidth: true
            visible: !kcm.isH264Supported()
            text: i18nc("@info:status", "Remote desktop cannot be enabled because your system does not support H264 video encoding. Please contact your distribution regarding how to enable it.")
        }

        Kirigami.InlineMessage {
            id: certificateError
            type: Kirigami.MessageType.Error
            position: Kirigami.InlineMessage.Position.Header
            Layout.fillWidth: true
            // TODO better text
            text: i18nc("@info:status", "Generating certificates automatically has failed!")
        }

        // Non-InlineMessage header content does need margins; put it all in here
        // so we can do that in a single place
        ColumnLayout {
            spacing: headerLayout.spacings
            Layout.margins: headerLayout.spacings

            QQC2.Label {
                text: i18n("Set up remote login to connect using apps supporting the “RDP” remote desktop protocol.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
                Layout.alignment: Qt.AlignHCenter
            }

            QQC2.Label {
                visible: toggleServerSwitch.checked
                text: i18nc("@info:usagetip", "Use any of the following addresses to connect to this device:")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
                Layout.alignment: Qt.AlignHCenter
            }

            // ...But here we want zero spacing again since otherwise the delegates
            // take up too much vertical space
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                visible: toggleServerSwitch.checked

                Repeater {
                    id: addressesRepeater
                    model: kcm.listenAddressList()

                    RowLayout {
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.SelectableLabel {
                            id: addressLabel
                            text: modelData
                            Layout.leftMargin: Kirigami.Units.gridUnit
                            Layout.alignment: Qt.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        QQC2.Button {
                            id: copyAddressButton
                            icon.name: "edit-copy-symbolic"
                            text: i18nc("@action:button", "Copy Address to Clipboard")
                            display: QQC2.AbstractButton.IconOnly
                            onClicked: {
                                kcm.copyAddressToClipboard(addressLabel.text);
                            }
                            QQC2.ToolTip {
                                text: copyAddressButton.text
                                visible: copyAddressButton.hovered || (Kirigami.Settings.tabletMode && copyAddressButton.pressed)
                            }
                        }
                    }
                }
            }
        }
    }

    view: ListView {
        id: userListView

        clip: true
        enabled: !toggleServerSwitch.checked

        headerPositioning: ListView.OverlayHeader
        header: Kirigami.InlineViewHeader {
            width: userListView.width
            text: i18nc("@title", "Usernames")
            actions: [
                Kirigami.Action {
                    icon.name: "list-add-symbolic"
                    text: i18nc("@action:button", "Add New…")
                    Accessible.name: i18nc("@action:button", "Add New User Account…")
                    onTriggered: source => {
                        root.addUser();
                    }
                }
            ]
        }

        Kirigami.PlaceholderMessage {
            width: parent.width - (Kirigami.Units.largeSpacing * 4)
            anchors.centerIn: parent
            visible: userListView.count === 0
            icon.name: "list-add-user-symbolic"
            text: i18nc("@info:placeholder", "Add at least one user account to enable remote login")
            explanation: xi18nc("@info:placeholder", "Click <interface>Add New…</interface> to add one")
        }

        model: settings.users

        delegate: QQC2.ItemDelegate {
            id: itemDelegate
            width: userListView.width
            text: modelData
            hoverEnabled: !toggleServerSwitch.checked
            // Help line up text and actions
            Kirigami.Theme.useAlternateBackgroundColor: true
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
                        visible: modifyUserButton.hovered || (Kirigami.Settings.tabletMode && modifyUserButton.pressed)
                    }
                }

                QQC2.Button {
                    id: deleteUserButton
                    icon.name: "edit-delete-remove-symbolic"
                    text: i18nc("@action:button", "Remove user…")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: {
                        root.deleteUser(itemDelegate.text);
                    }
                    QQC2.ToolTip {
                        text: deleteUserButton.text
                        visible: deleteUserButton.hovered || (Kirigami.Settings.tabletMode && deleteUserButton.pressed)
                    }
                }
            }
            onClicked: {
                root.modifyUser(itemDelegate.text);
            }
        }
    }

    footer: Kirigami.FormLayout {
        id: settingsLayout

        readonly property bool showAdvancedCertUI: !autoGenCertSwitch.checked

        enabled: !toggleServerSwitch.checked && userListView.count > 0

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("title:group Group of RDP server settings", "Server Settings")
        }

        QQC2.CheckBox {
            id: autostartOnLogin
            text: i18nc("@option:check", "Autostart on login")
            checked: settings.autostart
            onToggled: {
                settings.autostart = checked;
            }
            KCM.SettingStateBinding {
                configObject: settings
                settingName: "autostart"
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
                onMoved: {
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

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("title:group Group of RDP server settings", "Security Certificates")
        }

        QQC2.CheckBox {
            id: autoGenCertSwitch
            text: i18nc("@label:check generate security certificates automatically", "Generate automatically")
            checked: settings.autogenerateCertificates
            onToggled: {
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
            visible: settingsLayout.showAdvancedCertUI
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
            visible: settingsLayout.showAdvancedCertUI
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
