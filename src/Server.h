// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>

#include <QTcpServer>

#include "krdp_export.h"

namespace KRdp
{

class KRDP_EXPORT Server : public QTcpServer
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;

    void start();
    void stop();

protected:
    void incomingConnection(qintptr handle) override;

private:
    class Private;
    const std::unique_ptr<Private> d;
};

}
