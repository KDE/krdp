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
#include <QDBusConnection>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QProcess>
#include <qt6keychain/keychain.h>

#include "org.freedesktop.impl.portal.PermissionStore.h"

using namespace Qt::StringLiterals;

K_PLUGIN_CLASS_WITH_JSON(KRDPServerConfig, "kcm_krdpserver.json")

static const QString passwordServiceName = QLatin1StringView("KRDP");
static const QString dbusSystemdDestination = u"org.freedesktop.systemd1"_s;
static const QString dbusSystemdPath = u"/org/freedesktop/systemd1"_s;
static const QString dbusKrdpServerServicePath = u"/org/freedesktop/systemd1/unit/plasma_2dkrdp_5fserver_2eservice"_s;
static const QString dbusSystemdUnitInterface = u"org.freedesktop.systemd1.Unit"_s;
static const QString dbusSystemdManagerInterface = u"org.freedesktop.systemd1.Manager"_s;
static const QString dbusSystemdPropertiesInterface = u"org.freedesktop.DBus.Properties"_s;

KRDPServerConfig::KRDPServerConfig(QObject *parent, const KPluginMetaData &data)
    : KQuickManagedConfigModule(parent, data)
    , m_serverSettings(new KRDPServerSettings(this))
{
    setButtons(Help | Apply | Default);

    auto recorder = PipeWireRecord();
    m_isH264Supported = recorder.suggestedEncoders().contains(PipeWireRecord::H264Baseline);

    if (m_serverSettings->autogenerateCertificates()) {
        generateCertificate();
    }

    QDBusConnection::sessionBus().connect(dbusSystemdDestination,
                                          dbusKrdpServerServicePath,
                                          dbusSystemdPropertiesInterface,
                                          u"PropertiesChanged"_s,
                                          this,
                                          SLOT(servicePropertiesChanged()));
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

void KRDPServerConfig::createRestoreToken()
{
    static std::once_flag flag;
    std::call_once(flag, [this]() {
        auto iface = new OrgFreedesktopImplPortalPermissionStoreInterface(u"org.freedesktop.impl.portal.PermissionStore"_s,
                                                                          u"/org/freedesktop/impl/portal/PermissionStore"_s,
                                                                          QDBusConnection::sessionBus(),
                                                                          this);
        // WARNING: The app_id org.kde.krdpserver must match the service name on the systemd side!
        auto reply = iface->SetPermission(u"kde-authorized"_s, /* create = */ true, u"remote-desktop"_s, u"org.kde.krdpserver"_s, {u"yes"_s});
        auto watcher = new QDBusPendingCallWatcher(reply, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, iface]() {
            watcher->deleteLater();
            iface->deleteLater();
            QDBusPendingReply<> reply(*watcher);
            if (reply.isError()) {
                qCWarning(KRDPKCM) << "Failed to set pre-authorization in portal permission store" << reply.error().message();
            } else {
                qCDebug(KRDPKCM) << "Configured pre-authorization in portal permission store";
            }
        });
        return true;
    });
}

bool KRDPServerConfig::isH264Supported()
{
    return m_isH264Supported;
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

QString KRDPServerConfig::hostName() const
{
    QHostInfo info;
    return info.localHostName();
}

bool KRDPServerConfig::managementAvailable() const
{
    static bool managementAvailable = QDBusConnection::sessionBus().interface()->isServiceRegistered(u"org.freedesktop.systemd1"_s);
    return managementAvailable;
}

void KRDPServerConfig::toggleAutoconnect(const bool enabled)
{
    qDebug(KRDPKCM) << "Setting KRDP Server service autostart on login to " << enabled << "over QDBus";

    auto msg = QDBusMessage::createMethodCall(dbusSystemdDestination,
                                              dbusSystemdPath,
                                              dbusSystemdManagerInterface,
                                              enabled ? u"EnableUnitFiles"_s : u"DisableUnitFiles"_s);
    if (enabled) {
        msg.setArguments({QStringList(u"app-org.kde.krdpserver.service"_s), false, true});
    } else {
        msg.setArguments({QStringList(u"app-org.kde.krdpserver.service"_s), false});
    }
    QDBusConnection::sessionBus().asyncCall(msg);

    if (enabled) {
        createRestoreToken();
    }
}

void KRDPServerConfig::toggleServer(const bool enabled)
{
    auto msg = QDBusMessage::createMethodCall(dbusSystemdDestination, dbusKrdpServerServicePath, dbusSystemdUnitInterface, enabled ? u"Start"_s : u"Stop"_s);

    msg.setArguments({u"replace"_s});
    qDebug(KRDPKCM) << "Toggling KRDP Server to " << enabled << "over QDBus";
    QDBusConnection::sessionBus().asyncCall(msg);

    if (enabled) {
        createRestoreToken();
    }
}

void KRDPServerConfig::restartServer()
{
    qDebug(KRDPKCM) << "Restarting KRDP Server";
    auto restartMsg = QDBusMessage::createMethodCall(dbusSystemdDestination, dbusKrdpServerServicePath, dbusSystemdUnitInterface, u"Restart"_s);
    restartMsg.setArguments({u"replace"_s});
    auto pendingCall = QDBusConnection::sessionBus().asyncCall(restartMsg);
    auto watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [&](QDBusPendingCallWatcher *w) {
        checkServerRunning();
        w->deleteLater();
    });
}

void KRDPServerConfig::generateCertificate()
{
    if (!m_serverSettings->certificate().isEmpty() || !m_serverSettings->certificateKey().isEmpty()) {
        return;
    }
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).mkpath(u"krdpserver"_s);
    QString certificatePath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/krdpserver/krdp.crt"_s);
    QString certificateKeyPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/krdpserver/krdp.key"_s);
    qDebug(KRDPKCM) << "Generating certificate files to: " << certificatePath << " and " << certificateKeyPath;
    QProcess sslProcess;
    sslProcess.start(u"openssl"_s,
                     {
                         u"req"_s,
                         u"-nodes"_s,
                         u"-new"_s,
                         u"-x509"_s,
                         u"-keyout"_s,
                         certificateKeyPath,
                         u"-out"_s,
                         certificatePath,
                         u"-days"_s,
                         u"1"_s,
                         u"-batch"_s,
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

void KRDPServerConfig::checkServerRunning()
{
    // Checks if there is PID, and if there is, process is running.
    auto msg = QDBusMessage::createMethodCall(dbusSystemdDestination, dbusKrdpServerServicePath, dbusSystemdPropertiesInterface, u"Get"_s);
    msg.setArguments({u"org.freedesktop.systemd1.Service"_s, u"MainPID"_s});

    QDBusPendingCall pcall = QDBusConnection::sessionBus().asyncCall(msg);
    auto watcher = new QDBusPendingCallWatcher(pcall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [&](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariant> reply(*w);
        Q_EMIT serverRunning(reply.value().toInt() > 0 ? true : false);
        w->deleteLater();
    });
}

void KRDPServerConfig::copyAddressToClipboard(const QString &address)
{
    QGuiApplication::clipboard()->setText(address.trimmed());
}

void KRDPServerConfig::servicePropertiesChanged()
{
    checkServerRunning();
}

#include "kcmkrdpserver.moc"
