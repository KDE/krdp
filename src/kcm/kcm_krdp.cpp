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
static const QString passwordServiceName = QLatin1StringView("KRDP");

KRDPModule::KRDPModule(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
{
    setButtons(Apply);
    load();
}

QString KRDPModule::toLocalFile(const QUrl url)
{
    return url.toLocalFile();
}

// Save and load
void KRDPModule::load()
{
    KQuickConfigModule::load();
    KSharedConfig::Ptr config = KSharedConfig::openConfig(settingsFileName);

    KConfigGroup generalGroup(config, QStringLiteral("General"));

    setUsername(generalGroup.readEntry("Username", ""));
    setPort(generalGroup.readEntry("Port", 0));
    setCertPath(generalGroup.readEntry("CertPath", ""));
    setCertKeyPath(generalGroup.readEntry("CertKeyPath", ""));
    setQuality(generalGroup.readEntry("Quality", 100));
    readPassword();
}

void KRDPModule::save()
{
    KQuickConfigModule::save();
    KSharedConfig::Ptr config = KSharedConfig::openConfig(settingsFileName);

    KConfigGroup generalGroup(config, QStringLiteral("General"));

    generalGroup.writeEntry("Username", username());
    generalGroup.writeEntry("Port", port());
    generalGroup.writeEntry("CertPath", certPath());
    generalGroup.writeEntry("CertKeyPath", certKeyPath());
    generalGroup.writeEntry("Quality", quality());
    writePassword();
}

void KRDPModule::writePassword()
{
    const auto writeJob = new QKeychain::WritePasswordJob(passwordServiceName);
    writeJob->setKey(QLatin1StringView(("KRDP")));
    writeJob->setTextData(password());
    writeJob->start();
    if (writeJob->error()) {
        qWarning() << "requestPassword: Failed to write password because of error: " << writeJob->error();
    }
}

void KRDPModule::readPassword()
{
    const auto readJob = new QKeychain::ReadPasswordJob(passwordServiceName, this);
    readJob->setKey(QLatin1StringView("KRDP"));
    connect(readJob, &QKeychain::ReadPasswordJob::finished, this, [this, readJob]() {
        if (readJob->error() != QKeychain::Error::NoError) {
            qWarning() << "requestPassword: Failed to read password because of error: " << readJob->error();
            return;
        }
        setPassword(readJob->textData());
    });
    readJob->start();
}

// Getters
QString KRDPModule::username()
{
    return m_username;
}
QString KRDPModule::password()
{
    return m_password;
}
int KRDPModule::port()
{
    return m_port;
}
QString KRDPModule::certPath()
{
    return m_certPath;
}
QString KRDPModule::certKeyPath()
{
    return m_certKeyPath;
}
int KRDPModule::quality()
{
    return m_quality;
}

// Setters
void KRDPModule::setUsername(const QString &username)
{
    m_username = username;
}
void KRDPModule::setPassword(const QString &password)
{
    m_password = password;
}
void KRDPModule::setPort(const int &port)
{
    m_port = port;
}
void KRDPModule::setCertPath(const QString &certPath)
{
    m_certPath = certPath;
}
void KRDPModule::setCertKeyPath(const QString &certKeyPath)
{
    m_certKeyPath = certKeyPath;
}
void KRDPModule::setQuality(const int &quality)
{
    m_quality = quality;
}

#include "kcm_krdp.moc"
