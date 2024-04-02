// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.krdpserversettings.private 1.0
import org.kde.kcmutils as KCM

Kirigami.Dialog {
    id: deleteUserModal
    // if oldUsername is empty, we're adding a new user
    property string selectedUsername

    implicitWidth: Kirigami.Units.gridUnit * 15
    implicitHeight: Kirigami.Units.gridUnit * 10
    title: i18nc("@title:window", "Delete confirmation")

    footer: QQC2.DialogButtonBox {
        standardButtons: deleteButton | QQC2.DialogButtonBox.Cancel

        onDiscarded: {
            kcm.deleteUser(selectedUsername);
            deleteUserModal.close();
        }
        QQC2.Button {
            id: deleteButton
            icon.name: "delete"
            text: i18nc("@label:button", "Delete")
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.DestructiveRole
        }
    }

    QQC2.Label {
        text: i18nc("@info", "Are you sure you want to delete following user: <warning>%1</warning>?", selectedUsername)
        wrapMode: Text.Wrap
    }
}
