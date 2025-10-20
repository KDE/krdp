// SPDX-FileCopyrightText: 2025 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.PromptDialog {
    id: startupFailureDialog

    showCloseButton: false
    title: i18nc("@title:window", "Startup Failure")
    subtitle: i18nc("@info", "The RDP server failed to start. Make sure that there is at least one user enabled.")

    standardButtons: Kirigami.Dialog.Ok
}
