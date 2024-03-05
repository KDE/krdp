// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <KQuickConfigModule>

class KRDPModule : public KQuickConfigModule
{
    Q_OBJECT
public:
    KRDPModule(QObject *parent, const KPluginMetaData &data, const QVariantList &args);
};
