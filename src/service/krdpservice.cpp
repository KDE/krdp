/*
    SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krdpservice.h"

#include <kpluginfactory.h>

K_PLUGIN_CLASS_WITH_JSON(KRDPService, "krdpservice.json");

static const QString s_dbusServiceName = QStringLiteral("org.kde.plasma.krdp_service");

KRDPService::KRDPService(QObject *parent, const QList<QVariant> &)
    : KDEDModule(parent)
{
    setModuleName(QStringLiteral("KRDPService"));
}

KRDPService::~KRDPService()
{
}

#include "krdpservice.moc"

#include "moc_krdpservice.cpp"
