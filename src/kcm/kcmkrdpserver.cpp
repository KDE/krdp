// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kcmkrdpserver.h"
#include "krdpkcm_logging.h"
#include "krdpserverdata.h"
#include "krdpserversettings.h"
#include <PipeWireRecord>

#include <KPluginFactory>
#include <QClipboard>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QNetworkInterface>
#include <QProcess>
#include <qt6keychain/keychain.h>

K_PLUGIN_CLASS_WITH_JSON(KRDPServerConfig, "kcm_krdpserver.json")

static const QString passwordServiceName = QLatin1StringView("KRDP");

KRDPServerConfig::KRDPServerConfig(QObject *parent, const KPluginMetaData &data)
    : KQuickManagedConfigModule(parent, data)
    , m_serverSettings(new KRDPServerSettings(this))
{
    setButtons(Help | Apply | Default);
    isH264Supported();
    if (m_serverSettings->autogenerateCertificates()) {
        generateCertificate();
    }
}

KRDPServerConfig::~KRDPServerConfig() = default;

QString KRDPServerConfig::toLocalFile(const QUrl &url)
{
    return url.toLocalFile();
}

void KRDPServerConfig::readPasswordFromWallet(const QString &user)
{
    if (user.isEmpty()) {
        return;
    }
    const auto readJob = new QKeychain::ReadPasswordJob(passwordServiceName, this);
    readJob->setKey(QLatin1StringView(user.toLatin1()));
    connect(readJob, &QKeychain::ReadPasswordJob::finished, this, [this, user, readJob]() {
        if (readJob->error() != QKeychain::Error::NoError) {
            qWarning(KRDPKCM) << "requestPassword: Failed to read password of " << user << " because of error: " << readJob->errorString();
            Q_EMIT keychainError(readJob->errorString());
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
        qWarning(KRDPKCM) << "requestPassword: Failed to write password of " << user << " because of error: " << writeJob->errorString();
        Q_EMIT keychainError(writeJob->errorString());
    }
}

void KRDPServerConfig::deletePasswordFromWallet(const QString &user)
{
    const auto deleteJob = new QKeychain::DeletePasswordJob(passwordServiceName);
    deleteJob->setKey(QLatin1StringView(user.toLatin1()));
    deleteJob->start();
    if (deleteJob->error() != QKeychain::Error::NoError) {
        qWarning(KRDPKCM) << "requestPassword: Failed to delete password of " << user << " because of error: " << deleteJob->errorString();
        Q_EMIT keychainError(deleteJob->errorString());
    }
}

void KRDPServerConfig::addUser(const QString &username, const QString &password)
{
    if (!username.isEmpty()) {
        auto userList = m_serverSettings->users();
        userList.append(username);
        writePasswordToWallet(username, password);
        m_serverSettings->setUsers(userList);
    }
    save();
}

void KRDPServerConfig::modifyUser(const QString &oldUsername, const QString &newUsername, const QString &newPassword)
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

void KRDPServerConfig::deleteUser(const QString &username)
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

bool KRDPServerConfig::userExists(const QString &username)
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

bool KRDPServerConfig::isH264Supported()
{
    auto recorder = new PipeWireRecord(this);
    return recorder->suggestedEncoders().contains(PipeWireRecord::H264Baseline);
}

QStringList KRDPServerConfig::listenAddressList()
{
    QStringList addressList;
    // Prepare default address
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &interface : interfaces) {
        if (!interface.flags().testAnyFlag(QNetworkInterface::IsLoopBack)) {
            for (auto &address : interface.addressEntries()) {
                // Show only private ip addresses
                if (address.ip().isPrivateUse()) {
                    addressList.append(address.ip().toString());
                }
            }
        }
    }
    return addressList;
}

void KRDPServerConfig::toggleAutoconnect(const bool enabled)
{
    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"));

    qDebug(KRDPKCM) << "Setting KRDP Server service autostart on login to " << enabled << "over QDBus:";

    if (enabled) {
        qDebug(KRDPKCM) << manager.asyncCall(QStringLiteral("EnableUnitFiles"), QStringList(u"plasma-krdp_server.service"_qs), false, true).reply();
    } else {
        qDebug(KRDPKCM) << manager.asyncCall(QStringLiteral("DisableUnitFiles"), QStringList(u"plasma-krdp_server.service"_qs), false).reply();
    }
}

void KRDPServerConfig::toggleServer(const bool enabled)
{
    // No need to start the server again if it's already running
    if (enabled && isServerRunning()) {
        return;
    }
    QDBusInterface unit(QStringLiteral("org.freedesktop.systemd1"),
                        QStringLiteral("/org/freedesktop/systemd1/unit/plasma_2dkrdp_5fserver_2eservice"),
                        QStringLiteral("org.freedesktop.systemd1.Unit"));

    qDebug(KRDPKCM) << "Toggling KRDP Server to " << enabled << "over QDBus:";
    qDebug(KRDPKCM) << unit.asyncCall(enabled ? QStringLiteral("Start") : QStringLiteral("Stop"), QStringLiteral("replace")).reply();
}

void KRDPServerConfig::generateCertificate()
{
    if (!m_serverSettings->certificate().isEmpty() || !m_serverSettings->certificateKey().isEmpty()) {
        return;
    }
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).mkpath(QStringLiteral("krdpserver"));
    QString certificatePath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/krdpserver/krdp.crt"));
    QString certificateKeyPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/krdpserver/krdp.key"));
    qDebug(KRDPKCM) << "Generating certificate files to: " << certificatePath << " and " << certificateKeyPath;
    QProcess sslProcess;
    sslProcess.start(u"openssl"_qs,
                     {
                         u"req"_qs,
                         u"-nodes"_qs,
                         u"-new"_qs,
                         u"-x509"_qs,
                         u"-keyout"_qs,
                         certificateKeyPath,
                         u"-out"_qs,
                         certificatePath,
                         u"-days"_qs,
                         u"1"_qs,
                         u"-batch"_qs,
                     });
    sslProcess.waitForFinished();

    m_serverSettings->setCertificate(certificatePath);
    m_serverSettings->setCertificateKey(certificateKeyPath);

    // Check that the path is valid
    if (QFileInfo::exists(certificatePath) && QFileInfo::exists(certificateKeyPath)) {
        qDebug(KRDPKCM) << "Certificate generated; ready to accept connections.";
        Q_EMIT generateCertificateSucceeded();
    } else {
        qCritical(KRDPKCM) << "Could not generate a certificate. A valid TLS certificate and key should be provided.";
        Q_EMIT generateCertificateFailed();
    }

    m_serverSettings->save();
}

bool KRDPServerConfig::isServerRunning()
{
    // Checks if there is PID, and if there is, process is running.

    QDBusInterface msg(QStringLiteral("org.freedesktop.systemd1"),
                       QStringLiteral("/org/freedesktop/systemd1/unit/plasma_2dkrdp_5fserver_2eservice"),
                       QStringLiteral("org.freedesktop.DBus.Properties"));

    QDBusReply<QVariant> response = msg.asyncCall(QStringLiteral("Get"), QStringLiteral("org.freedesktop.systemd1.Service"), QStringLiteral("MainPID")).reply();
    auto pid = response.value().toInt();

    return pid > 0 ? true : false;
}

void KRDPServerConfig::copyAddressToClipboard(const QString &address)
{
    QGuiApplication::clipboard()->setText(address.trimmed());
}

#include "kcmkrdpserver.moc"
