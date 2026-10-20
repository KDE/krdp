// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Server.h"

#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QSslCertificate>
#include <QStandardPaths>

#include <freerdp/channels/channels.h>
#include <freerdp/freerdp.h>
#include <winpr/ssl.h>

#include "RdpConnection.h"

#include "krdp_logging.h"

using namespace KRdp;

class KRDP_NO_EXPORT Server::Private
{
public:
    std::vector<std::unique_ptr<RdpConnection>> sessions;
    rdp_settings *settings = nullptr;

    QHostAddress address = QHostAddress::LocalHost;
    quint16 port = 3389;

    QList<User> users;
    bool usePamAuthentication = false;

    bool autogenerateCertificate = false;
    std::filesystem::path tlsCertificate;
    std::filesystem::path tlsCertificateKey;
};

Server::Server(QObject *parent)
    : QTcpServer(parent)
    , d(std::make_unique<Private>())
{
    winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
}

Server::~Server()
{
    stop();
}

bool Server::start()
{
    if (const QDateTime certificateExpiryDate = getCertificateExpiry();
        !certificateExpiryDate.isValid() || !std::filesystem::exists(d->tlsCertificate) || !std::filesystem::exists(d->tlsCertificateKey)) {
        // If certificateExpiryDate is invalid, we have failed to open and read the certificate
        if (autogenerateCertificate()) {
            qCInfo(KRDP).nospace() << "A valid TLS certificate (" << QString::fromStdString(d->tlsCertificate.filename().string()) << ") and/or key ("
                                   << QString::fromStdString(d->tlsCertificateKey.filename().string())
                                   << ") could not be found. They will be automatically generated.";

            if (!generateCertificate()) {
                qCCritical(KRDP) << "Failed to generate TLS certificate and key.";
                return false;
            }
        } else {
            qCCritical(KRDP).nospace() << "A valid TLS certificate (" << QString::fromStdString(d->tlsCertificate.filename().string()) << ") and key ("
                                       << QString::fromStdString(d->tlsCertificateKey.filename().string()) << ") is required for the server to run!";
            return false;
        }
    } else {
        const auto now = QDateTime::currentDateTimeUtc();
        const auto certificateDays = now.daysTo(certificateExpiryDate);

        if (certificateDays <= 30) {
            const bool expired = now.secsTo(certificateExpiryDate) <= 0;

            qCWarning(KRDP).nospace() << "The TLS certificate (" << QString::fromStdString(d->tlsCertificate.filename().string())
                                      << (expired ? ") expired " : ") expires in ") << (expired ? -certificateDays : certificateDays)
                                      << (expired ? "days ago." : "days.")
                                      << (autogenerateCertificate() ? " It will be re-generated." : " It should be replaced.");

            if (!generateCertificate()) {
                qCWarning(KRDP) << "Failed to generate TLS certificate and key.";
            }
        }
    }

    if (!listen(d->address, d->port)) {
        // NOTE: We cannot use QTcpServer methods to get the server address and port because it won't initialize them if listen fails.
        qCCritical(KRDP) << "Unable to listen for connections on" << d->address << d->port;
        return false;
    }

    // FreeRDP3 tries to use a global instance of the settings object when
    // initializing a new peer. However, it seems to fail at actually creating a
    // global default instance. So create one here and use that.
    d->settings = freerdp_settings_new(FREERDP_SETTINGS_SERVER_MODE);

    qCDebug(KRDP) << "Listening for connections on" << serverAddress() << serverPort();
    return true;
}

void Server::stop()
{
    close();

    if (d->settings) {
        freerdp_settings_free(d->settings);
        d->settings = nullptr;
    }
}

QHostAddress Server::address() const
{
    return d->address;
}

void Server::setAddress(const QHostAddress &newAddress)
{
    if (newAddress == d->address) {
        return;
    }

    d->address = newAddress;
}

quint16 Server::port() const
{
    return d->port;
}

void Server::setPort(quint16 newPort)
{
    if (newPort == d->port) {
        return;
    }

    d->port = newPort;
}

QList<User> KRdp::Server::users() const
{
    return d->users;
}

void KRdp::Server::setUsers(const QList<User> &users)
{
    d->users = users;
}

void KRdp::Server::addUser(const User &user)
{
    d->users.append(user);
}

bool Server::usePAMAuthentication() const
{
    return d->usePamAuthentication;
}

void Server::setUsePAMAuthentication(bool usePAM)
{
    d->usePamAuthentication = usePAM;
}

bool Server::autogenerateCertificate() const
{
    return d->autogenerateCertificate;
}

void Server::setAutogenerateCertificate(const bool autogenerateCertificate)
{
    if (autogenerateCertificate == d->autogenerateCertificate) {
        return;
    }

    d->autogenerateCertificate = autogenerateCertificate;
}

std::filesystem::path Server::tlsCertificate() const
{
    return d->tlsCertificate;
}

void Server::setTlsCertificate(const std::filesystem::path &newTlsCertificate)
{
    if (newTlsCertificate == d->tlsCertificate) {
        return;
    }

    d->tlsCertificate = newTlsCertificate;
}

std::filesystem::path Server::tlsCertificateKey() const
{
    return d->tlsCertificateKey;
}

void Server::setTlsCertificateKey(const std::filesystem::path &newTlsCertificateKey)
{
    if (newTlsCertificateKey == d->tlsCertificateKey) {
        return;
    }

    d->tlsCertificateKey = newTlsCertificateKey;
}

void Server::incomingConnection(qintptr handle)
{
    auto session = std::make_unique<RdpConnection>(this, handle);
    auto sessionPtr = session.get();
    // queued: signal comes from the run thread, and it keeps the erase below from destroying the sender mid-emission
    connect(
        sessionPtr,
        &RdpConnection::stateChanged,
        this,
        [this, sessionPtr](RdpConnection::State state) {
            if (state == RdpConnection::State::Closed) {
                auto itr = std::find_if(d->sessions.begin(), d->sessions.end(), [sessionPtr](auto &session) {
                    return session.get() == sessionPtr;
                });
                if (itr == d->sessions.end()) {
                    return;
                }
                // extracted before erasing: ~RdpConnection can spin a nested event loop (KScreen calls
                // from destroyed() handlers) that re-enters this vector, so destroy it once consistent again
                auto session = std::move(*itr);
                d->sessions.erase(itr);
            }
        },
        Qt::QueuedConnection);
    d->sessions.push_back(std::move(session));
    Q_EMIT newConnectionCreated(sessionPtr);
}

bool Server::generateCertificate() const
{
    if (!QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).mkpath(QStringLiteral("krdpserver"))) {
        return false;
    };

    const QString qsCertificate = QString::fromStdString(d->tlsCertificate.string());
    const QString qsCertificateKey = QString::fromStdString(d->tlsCertificateKey.string());

    QProcess sslProcess;
    sslProcess.start(QStringLiteral("openssl"), {
       QStringLiteral("req"),
         QStringLiteral("-noenc"),
         QStringLiteral("-new"),
         QStringLiteral("-x509"),
         QStringLiteral("-out"), qsCertificate,
         QStringLiteral("-keyout"), qsCertificateKey,
         QStringLiteral("-days"), QStringLiteral("3650"),
         QStringLiteral("-batch")
    });
    sslProcess.waitForFinished();

    if (sslProcess.exitCode() != 0) {
        qCWarning(KRDP) << "OpenSSL error in certificate generation:" << sslProcess.readAllStandardError();
        return false;
    }

    Q_ASSERT(QFileInfo::exists(qsCertificate) && QFileInfo::exists(qsCertificateKey));

    return true;
}

QDateTime Server::getCertificateExpiry() const
{
    const QString qsCertificate = QString::fromStdString(d->tlsCertificate.string());

    QFile file(qsCertificate);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QSslCertificate certificate(&file, QSsl::Pem);
    if (certificate.isNull()) {
        return {};
    }

    const QDateTime expiryDate = certificate.expiryDate();
    if (!expiryDate.isValid()) {
        return {};
    }

    return expiryDate;
}

rdp_settings *Server::rdpSettings() const
{
    return d->settings;
}

#include "moc_Server.cpp"
