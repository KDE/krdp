// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kcm_krdp.h"

#include <KPluginFactory>
#include <kconfiggroup.h>
#include <qlatin1stringview.h>

K_PLUGIN_CLASS_WITH_JSON(KRDPModule, "kcm_krdp.json")

KRDPModule::KRDPModule(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : KQuickConfigModule(parent, data)
{
    setButtons(Apply);
    load();
}

void KRDPModule::load()
{
    KQuickConfigModule::load();
    KSharedConfig::Ptr config = KSharedConfig::openConfig(m_settingsFile);

    KConfigGroup generalGroup(config, QStringLiteral("General"));

    setUsername(generalGroup.readEntry("Username", ""));
    setPassword(generalGroup.readEntry("Password", ""));
    setPort(generalGroup.readEntry("Port", 0));
    setCertPath(generalGroup.readEntry("CertPath", ""));
    setCertKeyPath(generalGroup.readEntry("CertKeyPath", ""));
    setQuality(generalGroup.readEntry("Quality", 100));
}

void KRDPModule::save()
{
    KQuickConfigModule::save();
    KSharedConfig::Ptr config = KSharedConfig::openConfig(m_settingsFile);

    KConfigGroup generalGroup(config, QStringLiteral("General"));

    generalGroup.writeEntry("Username", getUsername());
    generalGroup.writeEntry("Password", getPassword());
    generalGroup.writeEntry("Port", getPort());
    generalGroup.writeEntry("CertPath", getCertPath());
    generalGroup.writeEntry("CertKeyPath", getCertKeyPath());
    generalGroup.writeEntry("Quality", getQuality());
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
    return m_certPath;
}
QString KRDPModule::getCertKeyPath()
{
    return m_certKeyPath;
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
    m_certPath = certPath;
    setNeedsSave(true);
}
void KRDPModule::setCertKeyPath(const QString &certKeyPath)
{
    m_certKeyPath = certKeyPath;
    setNeedsSave(true);
}
void KRDPModule::setQuality(const int &quality)
{
    m_quality = quality;
    setNeedsSave(true);
}

#include "kcm_krdp.moc"
