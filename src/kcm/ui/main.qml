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
            restartServerWarning.visible = toggleServerSwitch.checked;
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
        function onServerStatusChanged() : void {
            // TODO: why cant i access kcm.Status like kcm.Failed?
            if (kcm.serverStatus !== 1) {
                toggleServerSwitch.checked = false;
            }
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
            visible: kcm.managementAvailable
            onTriggered: source => {
                kcm.toggleServer(source.checked);
                if (!source.checked) {
                    // If we manually toggle the check off, always turn off the warning
                    restartServerWarning.visible = false;
                }
            }
            Component.onCompleted: {
                if (kcm.serverStatus === 1) {
                    toggleServerSwitch.checked = true;
                }
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

        RestartServerWarning {
            id: restartServerWarning
        }

        CodecError {}

        CertError {
            id: certificateError
        }

        Kirigami.InlineMessage {
            type: Kirigami.MessageType.Warning
            visible: !kcm.managementAvailable
            position: Kirigami.InlineMessage.Position.Header
            Layout.fillWidth: true
            text: i18nc("@info:status", "Systemd not found. krdpserver will require manual activation.")
        }

        Kirigami.InlineMessage {
            id: startupErrorMessage
            type: Kirigami.MessageType.Error
            visible: kcm.serverStatus === 3
            position: Kirigami.InlineMessage.Position.Header
            Layout.fillWidth: true
            text: i18nc("@info:status", "Error message from the RDP server:\n%1", kcm.errorMessage)
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
                visible: kcm.serverStatus === 1

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

    view: UserListView {
        id: userListView
    }

    footer: Kirigami.FormLayout {
        id: settingsLayout

        readonly property bool showAdvancedCertUI: !autoGenCertSwitch.checked

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("title:group Group of RDP server settings", "Server Settings")
        }

        QQC2.CheckBox {
            id: autostartOnLogin
            visible: kcm.managementAvailable
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
            enabled: userListView.count > 0
            Layout.preferredWidth: certKeyLayout.width

            Kirigami.FormData.label: i18nc("@label:textbox", "Video quality:")
            Kirigami.FormData.buddyFor: qualitySlider
            QQC2.Slider {
                id: qualitySlider
                Layout.fillWidth: true
                from: 50
                to: 100
                stepSize: 5
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
                    certLoader.selectKey = false;
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
                    certLoader.selectKey = true;
                    certLoader.active = true;
                }
            }
        }
    }

    CertLoader {
        id: certLoader
    }
}
