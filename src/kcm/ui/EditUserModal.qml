import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.krdpserversettings.private 1.0
import org.kde.kcmutils as KCM

Kirigami.OverlaySheet {
    id: editUserModal
    property string username

    implicitWidth: Kirigami.Units.gridUnit * 15
    implicitHeight: Kirigami.Units.gridUnit * 10
    header: Kirigami.Heading {
        text: username === "" ? i18nc("@title:window", "Add new user") : i18nc("@title:window", "Modify user")
    }

    footer: RowLayout {
        QQC2.Button {
            id: saveButton
            text: "Save"
            onClicked: {
                console.log("old user:", username, "new name", usernameField.text, " saved");
                editUserModal.close();
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
            text: editUserModal.username
            KCM.SettingStateBinding {
                configObject: Settings
                settingName: "users"
            }
        }

        Kirigami.PasswordField {
            id: passwordField
            Layout.maximumWidth: Kirigami.Units.gridUnit * 8
            echoMode: TextInput.Password
            Kirigami.FormData.label: i18nc("@label:textbox", "Password:")
            Component.onCompleted: {
                if (username !== "") {
                    kcm.readPasswordFromWallet(username);
                }
            }
            onTextEdited: {
                kcm.needsSave = true;
            }
        }
    }
}
