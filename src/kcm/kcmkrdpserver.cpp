// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kcmkrdpserver.h"
#include "krdpserverdata.h"
#include "krdpserversettings.h"

#include <KPluginFactory>
#include <qt6keychain/keychain.h>

K_PLUGIN_FACTORY_WITH_JSON(KRDPServerConfigFactory, "kcmkrdpserver.json", registerPlugin<KRDPServerConfig>(); registerPlugin<KRDPServerData>();)

static const QString passwordServiceName = QLatin1StringView("KRDP");

KRDPServerConfig::KRDPServerConfig(QObject *parent, const KPluginMetaData &data)
    : KQuickManagedConfigModule(parent, data)
{
    auto settings = new KRDPServerSettings(this);
    qmlRegisterSingletonInstance("org.kde.krdpserversettings.private", 1, 0, "Settings", settings);
    setButtons(Help | Apply | Default);
}

KRDPServerConfig::~KRDPServerConfig() = default;

QString KRDPServerConfig::toLocalFile(const QUrl url)
{
    return url.toLocalFile();
}

QString KRDPServerConfig::password(const QString &user)
{
    m_password.clear();
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

void KRDPServerConfig::setPassword(const QString &user, const QString &password)
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
