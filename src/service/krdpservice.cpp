/*
    SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "krdpservice.h"

#include <kpluginfactory.h>
#include <qdbusconnection.h>
#include <qdbusinterface.h>

K_PLUGIN_CLASS_WITH_JSON(KRDPService, "krdpservice.json");

static const QString s_dbusServiceName = QStringLiteral("org.kde.plasma.krdpservice");

KRDPService::KRDPService(QObject *parent, const QList<QVariant> &)
    : KDEDModule(parent)
{
    setModuleName(QStringLiteral("KRDPService"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(s_dbusServiceName);
    qDebug() << " Starting krdpservice";
}

KRDPService::~KRDPService()
{
    qDebug() << " Stopping krdpservice";
}

#include "krdpservice.moc"

#include "moc_krdpservice.cpp"
