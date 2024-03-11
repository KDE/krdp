// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kcm_krdp.h"

#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(KRDPModule, "kcm_krdp.json")

KRDPModule::KRDPModule(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : KQuickConfigModule(parent, data)
{
    setButtons(Apply);
}

void KRDPModule::load()
{
    // TODO load the config file and set the text fields to values
    KQuickConfigModule::load();
}
void KRDPModule::save()
{
    // TODO write the changes to config file
    KQuickConfigModule::save();
}

QString KRDPModule::getUsername()
{
    return m_username;
}
QString KRDPModule::getPassword()
{
    return m_password;
}
int KRDPModule::getPort()
{
    return m_port;
}
QString KRDPModule::getCertPath()
{
    return m_certPath.toString();
}
QString KRDPModule::getCertKeyPath()
{
    return m_certKeyPath.toString();
}
int KRDPModule::getQuality()
{
    return m_quality;
}

void KRDPModule::setUsername(const QString &username)
{
    m_username = username;
    setNeedsSave(true);
}
void KRDPModule::setPassword(const QString &password)
{
    m_password = password;
    setNeedsSave(true);
}
void KRDPModule::setPort(const int &port)
{
    m_port = port;
    setNeedsSave(true);
}
void KRDPModule::setCertPath(const QString &certPath)
{
    m_certPath = QUrl(certPath);
    setNeedsSave(true);
}
void KRDPModule::setCertKeyPath(const QString &certKeyPath)
{
    m_certKeyPath = QUrl(certKeyPath);
    setNeedsSave(true);
}
void KRDPModule::setQuality(const int &quality)
{
    m_quality = quality;
    setNeedsSave(true);
}

#include "kcm_krdp.moc"
