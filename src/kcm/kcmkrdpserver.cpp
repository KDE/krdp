// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kcmkrdpserver.h"

#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>
#include <qt6keychain/keychain.h>

K_PLUGIN_CLASS_WITH_JSON(KRDPServerConfig, "kcmkrdpserver.json")

static const QString settingsFileName = QLatin1StringView("krdprc");
static const QString settingsGroupName = QStringLiteral("General");
static const QString passwordServiceName = QLatin1StringView("KRDP");

KRDPServerConfig::KRDPServerConfig(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
    , m_config(KSharedConfig::openConfig(settingsFileName))
    , m_configGroup(KConfigGroup(m_config, settingsGroupName))
{
    setButtons(Apply);
    load();
}

QString KRDPServerConfig::toLocalFile(const QUrl url)
{
    return url.toLocalFile();
}

QString KRDPServerConfig::password()
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

void KRDPServerConfig::setPassword(const QString &password)
{
    const auto writeJob = new QKeychain::WritePasswordJob(passwordServiceName);
    writeJob->setKey(QLatin1StringView(("KRDP")));
    writeJob->setTextData(password);
    writeJob->start();
    if (writeJob->error() != QKeychain::Error::NoError) {
        qWarning() << "requestPassword: Failed to write password because of error: " << writeJob->error();
    }
}

#include "kcmkrdpserver.moc"
