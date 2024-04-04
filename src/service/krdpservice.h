/*
    SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <kdedmodule.h>

#include <QObject>

class QDBusServiceWatcher;

class KRDPService : public KDEDModule
{
    Q_OBJECT

public:
    KRDPService(QObject *parent, const QList<QVariant> &);
    ~KRDPService() override;
};
