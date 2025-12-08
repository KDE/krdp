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

            QQC2.ItemDelegate {
                id: itemDelegate

                width: userListView.width

                // Help line up text and actions
                Kirigami.Theme.useAlternateBackgroundColor: true

                onClicked: root.modifyUser(model.userName)

                contentItem: Kirigami.TitleSubtitleWithActions {
                    title: model.userName
                    elide: Text.ElideRight
                    selected: itemDelegate.pressed || itemDelegate.highlighted
                    displayHint: QQC2.Button.IconOnly
                    actions: [
                        Kirigami.Action {
                            icon.name: "edit-entry-symbolic"
                            text: i18nc("@action:button", "Modify user…")
                            onTriggered: {
                                itemDelegate.click();
                            }
                            tooltip: text
                        },
                        Kirigami.Action {
                            icon.name: "edit-delete-remove-symbolic"
                            text: i18nc("@action:button", "Remove user…")
                            onTriggered: {
                                root.deleteUser(model.userName);
                            }
                            tooltip: text
                        }
                    ]
                }
            }
        }
    }
}
