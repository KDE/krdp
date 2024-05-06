// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.PromptDialog {
    id: deleteUserModal
    // if oldUsername is empty, we're adding a new user
    property string selectedUsername
    title: i18nc("@title:window", "Discard user?")
    subtitle: i18nc("@info", "Are you sure you want to discard following user: %1?", selectedUsername)

    standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel

    onDiscarded: {
        kcm.deleteUser(selectedUsername);
        deleteUserModal.close();
    }
}
