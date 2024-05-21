// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

Kirigami.Dialog {
    id: editUserModal
    // if oldUsername is empty, we're adding a new user
    property string oldUsername
    property bool passwordChanged: false
    property bool usernameChanged: false
    property bool usernameAlreadyExistsError: false

    showCloseButton: false
    title: oldUsername === "" ? i18nc("@title:window", "Add new user") : i18nc("@title:window", "Modify user")

    Connections {
        target: kcm
        function onPasswordLoaded(user: string, password: string): void {
            if (user === editUserModal.oldUsername) {
                passwordField.text = password;
            }
        }
    }

    onAboutToShow: {
        passwordChanged = false;
        usernameChanged = false;
        usernameField.text = oldUsername;
        passwordField.text = "";
        kcm.readPasswordFromWallet(oldUsername);
    }

    function saveUser(): void {
        // add new user
        if (oldUsername === "") {
            kcm.addUser(usernameField.text, passwordField.text);
        } else
        // modify user
        if (usernameChanged || passwordChanged) {
            // Keep old username
            if (oldUsername === usernameField.text) {
                kcm.modifyUser(oldUsername, "", passwordField.text);
            } else
            // Change username
            {
                kcm.modifyUser(oldUsername, usernameField.text, passwordField.text);
            }
        }
        editUserModal.close();
    }

    footer: QQC2.DialogButtonBox {
        standardButtons: saveButton | QQC2.DialogButtonBox.Cancel
        onAccepted: {
            editUserModal.saveUser();
        }
        QQC2.Button {
            id: saveButton
            icon.name: "document-save"
            enabled: (editUserModal.usernameChanged || editUserModal.passwordChanged) && (usernameField.text !== "" && passwordField.text !== "") && !editUserModal.usernameAlreadyExistsError
            text: i18nc("@label:button", "Save")
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
        }
    }

    Kirigami.FormLayout {
        id: form
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

        QQC2.TextField {
            id: usernameField
            Kirigami.FormData.label: i18nc("@label:textbox", "Username:")
            Layout.fillWidth: true
            text: editUserModal.oldUsername
            KCM.SettingStateBinding {
                configObject: root.settings
                settingName: "users"
            }
            onTextEdited: {
                editUserModal.usernameChanged = usernameField.text !== editUserModal.oldUsername;
                editUserModal.usernameAlreadyExistsError = kcm.userExists(usernameField.text) && usernameField.text !== editUserModal.oldUsername;
            }
        }

        QQC2.Label {
            text: i18nc("@info", "Username already exists!")
            visible: editUserModal.usernameAlreadyExistsError
        }

        Kirigami.PasswordField {
            id: passwordField
            Kirigami.FormData.label: i18nc("@label:textbox", "Password:")
            Layout.fillWidth: true
            onTextEdited: {
                editUserModal.passwordChanged = true;
            }
        }
    }
}
