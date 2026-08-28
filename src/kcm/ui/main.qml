// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Layouts

import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.ScrollViewKCM {
    id: root

    property var settings: kcm.settings()

    // Used to avoid showing error when KCM is opened first time
    // and last run ended in an error, to avoid confusing users.
    property bool kcmJustOpened: true

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
            toggleServerSwitch.checked = kcm.isServerRunning();
            addressScrollView.visible = kcm.isServerRunning();
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
                root.kcmJustOpened = false;
                kcm.toggleServer(source.checked);
                if (!source.checked) {
                    // If we manually toggle the check off, always turn off the warning
                    restartServerWarning.visible = false;
                }
            }
            Component.onCompleted: {
                kcm.updateServerStatus();
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
            showCloseButton: true
            position: Kirigami.InlineMessage.Position.Header
            Layout.fillWidth: true
            text: xi18nc("@info:status", "Error message from the RDP server:<nl/>%1", kcm.errorMessage)
        }
        // Closing the error message breaks the visibility binding, so handle it separately here
        Binding {
            target: startupErrorMessage
            property: "visible"
            value: (kcm.errorMessage !== "" && !root.kcmJustOpened)
        }
        // Non-InlineMessage header content does need margins; put it all in here
        // so we can do that in a single place
        ColumnLayout {
            spacing: headerLayout.spacings
            Layout.margins: headerLayout.spacings

            QQC2.Label {
                text: i18n("Users listed below can log into this device remotely using apps that support the “RDP” protocol.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
                Layout.alignment: Qt.AlignHCenter
                visible: !toggleServerSwitch.checked
            }

            QQC2.Label {
                visible: toggleServerSwitch.checked
                text: i18nc("@info:usagetip", "Connect to this device at any of the following addresses:")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
                Layout.alignment: Qt.AlignHCenter
            }

            QQC2.ScrollView {
                id: addressScrollView
                implicitHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 4)
                visible: false
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                Flow {
                    id: serverAddressLayout
                    spacing: Kirigami.Units.largeSpacing
                    width: addressScrollView.availableWidth

                    Repeater {
                        id: addressesRepeater
                        model: kcm.listenAddressList()

                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.SelectableLabel {
                                id: addressLabel
                                text: modelData
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
    }

    // The form is in the list's own footer, not KCM.ScrollViewKCM's fixed page footer,
    // so it scrolls with the list instead of overflowing past a short window's height.
    view: UserListView {
        id: userListView

        footerPositioning: ListView.InlineFooter
        footer: Item {
            height: settingsFooter.height
        }

        // Due to strange behaviour with PullBackFooter, ListView's positioning and originY with the header,
        // we have ListView reserve scrolling room for the footer and position it manually with the following
        // horribly-derived expression for bottomMargin
        QQC2.ToolBar {
            id: settingsFooter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: userListView.contentY - userListView.originY + Math.min(0, userListView.height - userListView.contentHeight)

            width: userListView.width

            position: QQC2.ToolBar.Footer

            contentItem: Kirigami.Form {
                id: settingsLayout

                readonly property bool showAdvancedCertUI: !autoGenCertSwitch.checked

                Kirigami.FormGroup {
                    title: i18nc("@title:group", "Server Settings")

                    Kirigami.FormEntry {
                        title: i18nc("part of a sentence: 'Start server [automatically on login]'", "Start server:")
                        visible: kcm.managementAvailable

                        contentItem: QQC2.CheckBox {
                            text: i18nc("part of a sentence: 'Start server automatically on login'", "Automatically on login")
                            checked: settings.autostart
                            onToggled: settings.autostart = checked

                            KCM.SettingStateBinding {
                                configObject: settings
                                settingName: "autostart"
                            }
                        }
                    }

                    Kirigami.FormEntry {
                        title: i18n("Listening port:")

                        contentItem: QQC2.SpinBox {
                            id: portField

                            from: 1024
                            to: 65535
                            stepSize: 1
                            validator: IntValidator {
                                bottom: portField.from
                                top: portField.to
                            }

                            value: settings.listenPort
                            onValueModified: settings.listenPort = value

                            textFromValue: (value, locale) => {
                                // Commas should not appear in port numbers
                                locale.numberOptions = Locale.OmitGroupSeparator;
                                return Number(value).toLocaleString(locale, 'f', 0);
                            }

                            KCM.SettingStateBinding {
                                configObject: settings
                                settingName: "listenPort"
                            }
                        }
                    }

                    Kirigami.FormSeparator {}

                    Kirigami.FormEntry {
                        title: i18n("Operating mode:")
                        subtitle: i18nc("@info:usagetip", "Displays will be locked, and the remote user will see a virtual screen.")

                        contentItem: QQC2.CheckBox {
                            text: i18n("Enable exclusive mode")
                            checked: settings.exclusiveMode
                            onToggled: settings.exclusiveMode = checked

                            KCM.SettingStateBinding {
                                configObject: settings
                                settingName: "exclusiveMode"
                            }
                        }
                    }

                    Kirigami.FormSeparator {}

                    Kirigami.FormEntry {
                        title: i18n("Quality:")

                        contentItem: ColumnLayout {
                            Kirigami.FormData.buddyFor: qualitySlider

                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Slider {
                                id: qualitySlider
                                Layout.fillWidth: true

                                from: 50
                                to: 100
                                stepSize: 5

                                Kirigami.StyleHints.tickMarkStepSize: stepSize
                                snapMode: QQC2.Slider.SnapAlways

                                value: settings.quality
                                onMoved: settings.quality = value

                                KCM.SettingStateBinding {
                                    configObject: settings
                                    settingName: "quality"
                                }
                            }

                            RowLayout {
                                Layout.maximumWidth: qualitySlider.width
                                Layout.fillWidth: true
                                Layout.bottomMargin: qualitySlider.Layout.topMargin

                                spacing: 0

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: i18nc("Animation speed", "Responsiveness")
                                    textFormat: Text.PlainText
                                    horizontalAlignment: Text.AlignLeft
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: i18nc("Animation speed", "Quality")
                                    textFormat: Text.PlainText
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                        }
                    }

                    Kirigami.FormEntry {
                        contentItem: QQC2.CheckBox {
                            text: i18n("Automatically adjust quality for network conditions")
                            checked: settings.adaptiveQuality
                            onToggled: settings.adaptiveQuality = checked

                            KCM.SettingStateBinding {
                                configObject: settings
                                settingName: "adaptiveQuality"
                            }
                        }
                    }
                }

                Kirigami.FormGroup {
                    title: i18nc("@title:group", "Security Certificates")

                    Kirigami.FormEntry {
                        title: i18nc("part of a sentence: 'Generate certificates [automatically]'", "Generate certificates:")

                        contentItem: QQC2.CheckBox {
                            id: autoGenCertSwitch

                            text: i18nc("part of a sentence: '[Generate certificates] automatically'", "Automatically")
                            checked: settings.autogenerateCertificates
                            onCheckedChanged: {
                                settings.autogenerateCertificates = checked;
                                if (checked) {
                                    kcm.generateCertificate();

                                    certPathField.text = kcm.defaultCertificatePath;
                                    certKeyPathField.text = kcm.defaultCertificateKeyPath;
                                }
                            }

                            KCM.SettingStateBinding {
                                configObject: settings
                                settingName: "autogenerateCertificates"
                            }
                        }
                    }

                    Kirigami.FormEntry {
                        title: i18n("Certificate path:")
                        visible: settingsLayout.showAdvancedCertUI

                        contentItem: RowLayout {
                            Kirigami.FormData.buddyFor: certPathField

                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.ActionTextField {
                                id: certPathField
                                placeholderText: i18nc("@info:placeholder", "Enter certificate path…")

                                rightActions: Kirigami.Action {
                                    icon.name: "edit-clear-symbolic"
                                    visible: certPathField.text !== ""
                                    onTriggered: {
                                        certPathField.text = ""
                                    }
                                }

                                text: settings.certificate
                                onAccepted: settings.certificate = text

                                KCM.SettingHighlighter {
                                    highlight: certPathField.text !== kcm.defaultCertificatePath
                                }
                            }

                            QQC2.Button {
                                icon.name: "folder-open-symbolic"
                                text: i18nc("@action:button", "Choose certificate file…")
                                display: QQC2.AbstractButton.IconOnly

                                onClicked: {
                                    certLoader.selectKey = false;
                                    certLoader.active = true;
                                }

                                QQC2.ToolTip.visible: hovered || activeFocus
                                QQC2.ToolTip.text: text
                                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                                Accessible.description: i18n("Opens a file picker for the certificate file")
                                Accessible.onPressAction: onClicked()
                            }
                        }
                    }

                    Kirigami.FormEntry {
                        title: i18n("Certificate key path:")
                        visible: settingsLayout.showAdvancedCertUI

                        contentItem: RowLayout {
                            Kirigami.FormData.buddyFor: certKeyPathField

                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.ActionTextField {
                                id: certKeyPathField
                                placeholderText: i18nc("@info:placeholder", "Enter certificate key path…")

                                rightActions: Kirigami.Action {
                                    icon.name: "edit-clear-symbolic"
                                    visible: certKeyPathField.text !== ""
                                    onTriggered: {
                                        certKeyPathField.text = ""
                                    }
                                }

                                text: settings.certificateKey
                                onAccepted: settings.certificateKey = text

                                KCM.SettingHighlighter {
                                    highlight: certKeyPathField.text !== kcm.defaultCertificateKeyPath
                                }
                            }

                            QQC2.Button {
                                icon.name: "folder-open-symbolic"
                                text: i18nc("@action:button", "Choose certificate key file…")
                                display: QQC2.AbstractButton.IconOnly

                                onClicked: {
                                    certLoader.selectKey = true;
                                    certLoader.active = true;
                                }

                                QQC2.ToolTip.visible: hovered || activeFocus
                                QQC2.ToolTip.text: text
                                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                                Accessible.description: i18n("Opens a file picker for the certificate key file")
                                Accessible.onPressAction: onClicked()
                            }
                        }
                    }
                }
            }
        }
    }

    CertLoader {
        id: certLoader
    }
}
