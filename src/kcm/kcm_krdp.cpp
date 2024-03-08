// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kcm_krdp.h"

#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(KRDPModule, "kcm_krdp.json")

KRDPModule::KRDPModule(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : KQuickConfigModule(parent, data)
{
    setButtons(Help | Apply | Default);
}

void KRDPModule::installCertificateFromFile(const QUrl &url, const bool key)
{
    qDebug() << "Loading certificate file: " << url;
    qDebug() << "Is a key? " << key;
}

#include "kcm_krdp.moc"
