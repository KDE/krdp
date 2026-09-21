// SPDX-FileCopyrightText: 2026 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <QObject>

#include "krdp_export.h"

struct s_rdpei_server_context;
typedef struct s_rdpei_server_context RdpeiServerContext;

namespace KRdp
{

class RdpConnection;

class KRDP_EXPORT EiInputHandler : public QObject
{
public:
    explicit EiInputHandler(RdpConnection *session);
    ~EiInputHandler() override;

    bool initialize();
    void close();

private:
    RdpConnection *m_session = nullptr;
    RdpeiServerContext *m_context = nullptr;
};

}
