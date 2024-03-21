import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.krdpserversettings.private 1.0
import org.kde.kcmutils as KCM

Kirigami.OverlaySheet {
    id: deleteUserModal
    // if oldUsername is empty, we're adding a new user
    property string selectedUsername

    implicitWidth: Kirigami.Units.gridUnit * 15
    implicitHeight: Kirigami.Units.gridUnit * 10
    header: Kirigami.Heading {
        text: i18nc("@title:window", "Delete confirmation")
    }

    footer: RowLayout {
        QQC2.Button {
            id: deleteButton
            text: "Delete"
            onClicked: {
                kcm.deleteUser(selectedUsername);
                deleteUserModal.close();
            }
        }
        QQC2.Button {
            id: cancelButton
            text: "Cancel"
            onClicked: {
                deleteUserModal.close();
            }
        }
    }
    QQC2.Label {
        text:  i18nc("@info", "Are you sure you want to delete following user: <warning>%1</warning>?", selectedUsername)
        wrapMode: Text.Wrap
    }
}
