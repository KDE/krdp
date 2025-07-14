// SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "freerdp/server/disp.h"
#include <QObject>

#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

class KRDP_EXPORT DisplayControl : public QObject
{
    Q_OBJECT
public:
    explicit DisplayControl(RdpConnection *session);
    ~DisplayControl() override;

    bool initialize();

Q_SIGNALS:
    void requestedScreenSizeChanged(const QSize &size);

private:
    RdpConnection *m_session = nullptr;
    DispServerContext *m_dispManager = nullptr;
};

}
