import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.krdpserversettings.private 1.0
import org.kde.kcmutils as KCM

Kirigami.OverlaySheet {
    id: editUserModal
    // if oldUsername is empty, we're adding a new user
    property string oldUsername
    property bool passwordChanged: false
    property bool usernameChanged: false

    implicitWidth: Kirigami.Units.gridUnit * 15
    implicitHeight: Kirigami.Units.gridUnit * 10
    header: Kirigami.Heading {
        text: oldUsername === "" ? i18nc("@title:window", "Add new user") : i18nc("@title:window", "Modify user")
    }

    Connections {
        target: kcm
        function onPasswordLoaded(user: string, password: string): void {
            if (user === oldUsername) {
                passwordField.text = password;
            }
        }
    }

    onAboutToShow: {
        userExistsWarning.visible = false;
        passwordChanged = false;
        usernameChanged = false;
        usernameField.text = oldUsername;
        passwordField.text = "";
        kcm.readPasswordFromWallet(oldUsername);
    }

    function saveUser(): void {
        // add new user
        if (oldUsername === "") {
            if (!kcm.userExists(usernameField.text)) {
                kcm.addUser(usernameField.text, passwordField.text);
                editUserModal.close();
            } else {
                userExistsWarning.visible = true;
            }
        } else
        // modify user
        if (usernameChanged || passwordChanged) {
            // Keep old username
            if (oldUsername === usernameField.text) {
                kcm.modifyUser(oldUsername, "", passwordField.text);
                editUserModal.close();
            } else
            // Change username
            {
                if (!kcm.userExists(usernameField.text)) {
                    kcm.modifyUser(oldUsername, usernameField.text, passwordField.text);
                    editUserModal.close();
                } else {
                    userExistsWarning.visible = true;
                }
            }
        }
    }

    footer: RowLayout {
        QQC2.Button {
            id: saveButton
            enabled: (usernameChanged || passwordChanged) && (usernameField.text !== "" && passwordField.text !== "")
            text: "Save"
            onClicked: {
                editUserModal.saveUser();
            }
        }
    }

    Kirigami.FormLayout {
        id: form
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

        QQC2.TextField {
            id: usernameField
            Layout.maximumWidth: Kirigami.Units.gridUnit * 8
            Kirigami.FormData.label: i18nc("@label:textbox", "Username:")
            text: editUserModal.oldUsername
            KCM.SettingStateBinding {
                configObject: Settings
                settingName: "users"
            }
            onTextEdited: {
                usernameChanged = true;
                userExistsWarning.visible = false;
            }
        }

        QQC2.Label {
            id: userExistsWarning
            text: i18nc("@info", "Username already exists!")
            visible: false
            onVisibleChanged: {
                saveButton.enabled = !userExistsWarning.visible;
            }
        }

        Kirigami.PasswordField {
            id: passwordField
            Layout.maximumWidth: Kirigami.Units.gridUnit * 8
            echoMode: TextInput.Password
            Kirigami.FormData.label: i18nc("@label:textbox", "Password:")
            onTextEdited: {
                passwordChanged = true;
            }
        }
    }
}