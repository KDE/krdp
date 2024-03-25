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
    , m_serverSettings(new KRDPServerSettings(this))
{
    qmlRegisterSingletonInstance("org.kde.krdpserversettings.private", 1, 0, "Settings", m_serverSettings);
    setButtons(Help | Apply | Default);
    load();
}

KRDPServerConfig::~KRDPServerConfig() = default;

QString KRDPServerConfig::toLocalFile(const QUrl url)
{
    return url.toLocalFile();
}

void KRDPServerConfig::readPasswordFromWallet(const QString &user)
{
    const auto readJob = new QKeychain::ReadPasswordJob(passwordServiceName, this);
    readJob->setKey(QLatin1StringView(user.toLatin1()));
    connect(readJob, &QKeychain::ReadPasswordJob::finished, this, [this, user, readJob]() {
        if (readJob->error() != QKeychain::Error::NoError) {
            qWarning() << "requestPassword: Failed to read password of " << user << " because of error: " << readJob->error();
            return;
        }
        Q_EMIT passwordLoaded(user, readJob->textData());
    });
    readJob->start();
}

void KRDPServerConfig::writePasswordToWallet(const QString &user, const QString &password)
{
    const auto writeJob = new QKeychain::WritePasswordJob(passwordServiceName);
    writeJob->setKey(QLatin1StringView(user.toLatin1()));
    writeJob->setTextData(password);
    writeJob->start();
    if (writeJob->error() != QKeychain::Error::NoError) {
        qWarning() << "requestPassword: Failed to write password of " << user << " because of error: " << writeJob->error();
    }
}

void KRDPServerConfig::deletePasswordFromWallet(const QString &user)
{
    const auto deleteJob = new QKeychain::DeletePasswordJob(passwordServiceName);
    deleteJob->setKey(QLatin1StringView(user.toLatin1()));
    deleteJob->start();
    if (deleteJob->error() != QKeychain::Error::NoError) {
        qWarning() << "requestPassword: Failed to delete password of " << user << " because of error: " << deleteJob->error();
    }
}

void KRDPServerConfig::addUser(const QString username, const QString password)
{
    if (!username.isEmpty()) {
        auto userList = m_serverSettings->users();
        userList.append(username);
        writePasswordToWallet(username, password);
        m_serverSettings->setUsers(userList);
    }
    save();
}

void KRDPServerConfig::modifyUser(const QString oldUsername, const QString newUsername, const QString newPassword)
{
    // Don't do anything if the new user is already in the list as a failsafe
    if (userExists(newUsername)) {
        return;
    }
    // If we have new username, we're removing the old one and adding the new one
    if (!newUsername.isEmpty()) {
        auto userList = m_serverSettings->users();
        if (userList.contains(oldUsername)) {
            userList.removeAll(oldUsername);
        }
        userList.append(newUsername);
        deletePasswordFromWallet(oldUsername);
        writePasswordToWallet(newUsername, newPassword);
        m_serverSettings->setUsers(userList);
    }
    // We change the password of the old user
    else {
        if (!oldUsername.isEmpty()) {
            writePasswordToWallet(oldUsername, newPassword);
        }
    }
    save();
}

void KRDPServerConfig::deleteUser(const QString username)
{
    // Remove the old username
    if (!username.isEmpty()) {
        auto userList = m_serverSettings->users();
        if (userList.contains(username)) {
            userList.removeAll(username);
        }
        deletePasswordFromWallet(username);
        m_serverSettings->setUsers(userList);
    }
    save();
}

bool KRDPServerConfig::userExists(const QString username)
{
    return m_serverSettings->users().contains(username);
}

void KRDPServerConfig::save()
{
    KQuickManagedConfigModule::save();
    Q_EMIT krdpServerSettingsChanged();
}

void KRDPServerConfig::defaults()
{
    // Do not reset list of users. This would not be needed
    // if we can get the list of users from wallet, instead
    // of saving it in this config
    auto userList = m_serverSettings->users();
    KQuickManagedConfigModule::defaults();
    m_serverSettings->setUsers(userList);
}

#include "kcmkrdpserver.moc"
