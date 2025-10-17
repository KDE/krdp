// SPDX-FileCopyrightText: 2025 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import QtQml.Models as QtModels
import org.kde.kirigami as Kirigami
import org.kde.kirigami.delegates as KD
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

    section.property: "systemUser"
    section.delegate: Kirigami.ListSectionHeader {
        width: userListView.width
        // The attached section is cast to a string by Qt, so we have to compare our boolean as a string
        text: section == "true" ? i18nc("@title:group", "System Users") : i18nc("@title:group", "Other Users")
    }

    delegate: QtModels.DelegateChooser {

        role: "systemUser"

        // System user account
        QtModels.DelegateChoice {
            roleValue: "true"

            KD.CheckSubtitleDelegate {
                id: checkDelegate

                width: userListView.width
                icon.width: 0

                text: model.userName
                subtitle: i18nc("@info:usagetip used as a subtitle for a title+subtitle list item", "Login with your system password")

                // Help line up text and actions
                Kirigami.Theme.useAlternateBackgroundColor: true

                checked: model.systemUserEnabled
                onToggled: model.systemUserEnabled = checked
            }
        }

        // Manually-created user account
        QtModels.DelegateChoice {
            roleValue: "false"

            // Hand-rolled delegate to get some action buttons on the trailing side
            QQC2.ItemDelegate {
                id: itemDelegate

                width: userListView.width
                highlighted: pressed || down
                text: model.userName

                // Help line up text and actions
                Kirigami.Theme.useAlternateBackgroundColor: true

                onClicked: root.modifyUser(itemDelegate.text);

                contentItem: RowLayout {
                    spacing: Kirigami.Units.mediumSpacing

                    Kirigami.TitleSubtitle {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        title: itemDelegate.text
                        elide: Text.ElideRight
                        selected: itemDelegate.highlighted
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
            }
        }
    }
}
