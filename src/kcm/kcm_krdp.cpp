// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kcm_krdp.h"

#include <KPluginFactory>
#include <KSharedConfig>
#include <kconfiggroup.h>
#include <qlatin1stringview.h>
#include <qt6keychain/keychain.h>

K_PLUGIN_CLASS_WITH_JSON(KRDPModule, "kcm_krdp.json")

static const QString settingsFileName = QLatin1StringView("krdprc");
static const QString settingsGroupName = QStringLiteral("General");
static const QString passwordServiceName = QLatin1StringView("KRDP");

KRDPModule::KRDPModule(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
    , m_config(KSharedConfig::openConfig(settingsFileName))
    , m_configGroup(KConfigGroup(m_config, settingsGroupName))
{
    setButtons(Apply);
    load();
}

QString KRDPModule::toLocalFile(const QUrl url)
{
    return url.toLocalFile();
}

// Getters
QString KRDPModule::username()
{
    return m_configGroup.readEntry("Username", "");
}

QString KRDPModule::password()
{
    const auto readJob = new QKeychain::ReadPasswordJob(passwordServiceName, this);
    readJob->setKey(QLatin1StringView("KRDP"));
    connect(readJob, &QKeychain::ReadPasswordJob::finished, this, [this, readJob]() {
        if (readJob->error() != QKeychain::Error::NoError) {
            qWarning() << "requestPassword: Failed to read password because of error: " << readJob->error();
            return;
        }
        m_password = readJob->textData();
    });
    readJob->start();
    return m_password;
}

int KRDPModule::port()
{
    return m_configGroup.readEntry("Port", 123);
}

QString KRDPModule::certPath()
{
    return m_configGroup.readEntry("CertPath", "");
}

QString KRDPModule::certKeyPath()
{
    return m_configGroup.readEntry("CertKeyPath", "");
}

int KRDPModule::quality()
{
    return m_configGroup.readEntry("Quality", 100);
}

// Setters
void KRDPModule::setUsername(const QString &username)
{
    m_configGroup.writeEntry("Username", username);
}

void KRDPModule::setPassword(const QString &password)
{
    const auto writeJob = new QKeychain::WritePasswordJob(passwordServiceName);
    writeJob->setKey(QLatin1StringView(("KRDP")));
    writeJob->setTextData(password);
    writeJob->start();
    if (writeJob->error() != QKeychain::Error::NoError) {
        qWarning() << "requestPassword: Failed to write password because of error: " << writeJob->error();
    }
}

void KRDPModule::setPort(const int &port)
{
    m_configGroup.writeEntry("Port", port);
}

void KRDPModule::setCertPath(const QString &certPath)
{
    m_configGroup.writeEntry("CertPath", certPath);
}

void KRDPModule::setCertKeyPath(const QString &certKeyPath)
{
    m_configGroup.writeEntry("CertKeyPath", certKeyPath);
}

void KRDPModule::setQuality(const int &quality)
{
    m_configGroup.writeEntry("Quality", quality);
}

#include "kcm_krdp.moc"
