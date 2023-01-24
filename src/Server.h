// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#ifndef KRDP_SERVER_H
#define KRDP_SERVER_H

#include <memory>

#include <QObject>

#include "krdp_export.h"

namespace KRdp
{

class KRDP_EXPORT Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;

private:
    class Private;
    const std::unique_ptr<Private> d;
};

}

#endif // KRDP_SERVER_H
