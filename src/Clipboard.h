// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <QMimeData>
#include <QObject>

#include <freerdp/freerdp.h>
#include <freerdp/server/cliprdr.h>

#include <mutex>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

class KRDP_EXPORT Clipboard : public QObject
{
    Q_OBJECT

public:
    explicit Clipboard(RdpConnection *session);
    ~Clipboard() override;

    bool initialize();
    void close();

    bool enabled();

    void setServerData(const QMimeData *data);

    Q_SIGNAL void clientDataChanged();
    std::unique_ptr<QMimeData> getClipboard() const;

private:
    void sendServerData();

    class Private;
    const std::unique_ptr<Private> d;
};
}
