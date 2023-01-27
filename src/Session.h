// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <thread>

#include <QObject>

#include "krdp_export.h"

namespace KRdp
{

class KRDP_EXPORT Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(qintptr socketHandle);
    ~Session() override;

    void close();

private:
    void run(std::stop_token stopToken);

    class Private;
    const std::unique_ptr<Private> d;
};

}
