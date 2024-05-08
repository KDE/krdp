// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.PromptDialog {
    id: keychainErrorDialog

    property string errorText

    showCloseButton: false
    title: i18nc("@title:window", "Keychain error")
    subtitle: i18nc("@info", "Received following error with password keychain: %1", errorText)

    standardButtons: Kirigami.Dialog.Ok
}
