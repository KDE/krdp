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

ListView {
    id: userListView
    clip: true
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

    model: kcm.users

    section.property: "isSystemUser"
    section.delegate: Kirigami.ListSectionHeader {
        text: section ? i18n("System users") : i18n("Other users")
    }

    delegate: QQC2.ItemDelegate {
        id: itemDelegate
        required property string userName
        required property bool isSystemUser

        width: userListView.width
        text: userName
        // Help line up text and actions
        Kirigami.Theme.useAlternateBackgroundColor: true
        contentItem: RowLayout {
            spacing: Kirigami.Units.mediumSpacing

            QQC2.CheckBox {
                visible: itemDelegate.isSystemUser
            }

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
                visible: !itemDelegate.isSystemUser
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
                visible: !itemDelegate.isSystemUser
            }
        }
        onClicked: {
            root.modifyUser(itemDelegate.text);
        }
    }
}
