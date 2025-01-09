// SPDX-FileCopyrightText: 2025 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard 1 as FormCard
import org.kde.kcmutils as KCM

FormCard.FormCardPage {
    id: root

    property var settings: kcm.settings()
    property bool avoidRestartWarning: false

    KCM.ConfigModule.buttons: KCM.ConfigModule.NoAdditionalButton

    EditUserModal {
        id: editUserModal
        parent: root
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
            // may get triggered on enable/disable, which already autostarts
            if (root.avoidRestartWarning) {
                root.avoidRestartWarning = false;
            } else {
                restartServerWarning.visible = enableServer.checked;
            }
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
            enableServer.checked = isServerRunning;
        }
    }

    function modifyUser(user: string): void {
        editUserModal.oldUsername = user;
        editUserModal.open();
        // FIXME: warn to restart server?
    }

    function addUser(): void {
        modifyUser("");
        // FIXME: warn to restart server?
    }

    function deleteUser(user: string): void {
        deleteUserModal.selectedUsername = user;
        deleteUserModal.open();
        // FIXME: warn to restart server?
    }

    // Non-InlineMessage header content does need margins; put it all in here
    // so we can do that in a single place
    ColumnLayout {
        RestartServerWarning {
            id: restartServerWarning
        }

        CodecError {}

        CertError {
            id: certificateError
        }

        FormCard.FormHeader {
            title: i18nc("@title:group", "Remote Desktop Access")
        }

        FormCard.FormCard {
            padding: Math.round(Kirigami.Units.gridUnit / 2)

            FormCard.FormTextDelegate {
                description: i18n("Set up remote login to connect using apps supporting the “RDP” remote desktop protocol.")
            }
            FormCard.FormSwitchDelegate {
                id: enableServer
                text: i18nc("@option:check", "Enable RDP Server")
                checked: settings.autostart
                onCheckedChanged: {
                    kcm.toggleServer(enableServer.checked);
                    if (!enableServer.checked) {
                        // If we manually toggle the check off, always turn off the warning
                        restartServerWarning.visible = false;
                    }
                    // the mobile kcm combines autostart and enable, since we're already restarting
                    // triggered here, we do not show the warning for this setting
                    root.avoidRestartWarning = true;
                    settings.autostart = checked;
                }
                KCM.SettingStateBinding {
                    configObject: settings
                    settingName: "autostart"
                }
            }
            FormCard.FormDelegateSeparator {
                visible: settings.autostart
            }

            FormCard.FormTextDelegate {
                description: i18n("Use any of the following addresses to connect to this device:")
                visible: settings.autostart
            }

            Repeater {
                id: addressesRepeater
                model: kcm.listenAddressList()

                RowLayout {
                    spacing: Kirigami.Units.mediumSpacing
                    visible: settings.autostart

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

        FormCard.FormHeader {
            title: i18nc("@title:group", "Server Settings")
        }

        FormCard.FormCard {
            UserListView {
                id: userListView
                implicitHeight: Kirigami.Units.gridUnit * 16
                implicitWidth: parent.width
            }
        }
    }
}
