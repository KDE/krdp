// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Server.h"

#include <fstream>
#include <vector>

#include <QCoreApplication>
#include <QFile>
#include <QProcess>

#include <freerdp/channels/channels.h>
#include <freerdp/freerdp.h>
#include <winpr/ssl.h>

#include "FreeRDPLogging.h"
#include "RdpConnection.h"
#include "TlsHelpers.h"

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

    template<typename T>
    struct TlsItem {
        std::filesystem::path path;
        T value;
    };
    TlsItem<QSslCertificate> tlsCertificate;
    TlsItem<QSslKey> tlsKey;

    bool autogenerateCertificate = false;
};

Server::Server(QObject *parent)
    : QTcpServer(parent)
    , d(std::make_unique<Private>())
{
    winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());

    installFreeRDPLogging();
}

Server::~Server()
{
    stop();
}

bool Server::start()
{
    if (d->tlsCertificate.value.isNull() || d->tlsKey.value.isNull()) {
        if (d->autogenerateCertificate) {
            qCWarning(KRDP) << "The TLS certificate and/or key are either invalid or missing: they will be automatically generated";

            if (!generateCertificate()) {
                qCCritical(KRDP) << "Failed to generate TLS certificate and key";
                return false;
            }
        } else {
            qCCritical(KRDP) << "The TLS certificate and/or key are either invalid or missing: they are required for the server to run!";
            return false;
        }
    }

    // Test and output certificate effective/expired
    {
        // We do not error out if the certificate is not valid, these are only warnings and
        // used to check if our auto-generated certificate needs to be regenerated
        const auto now = QDateTime::currentDateTimeUtc();

        const QDateTime certificateEffectiveDateTime = d->tlsCertificate.value.effectiveDate();
        const bool isEffective = now >= certificateEffectiveDateTime;
        if (!isEffective) {
            qCWarning(KRDP) << "The TLS certificate is not yet effective:" << (d->autogenerateCertificate ? "it will be regenerated" : "it should be replaced");
        }

        const QDateTime certificateExpiryDateTime = d->tlsCertificate.value.expiryDate();
        const auto certificateDays = now.secsTo(certificateExpiryDateTime) / 86400; // daysTo does not round as wanted
        const bool isExpiredOrCloseTo = certificateDays <= 30;
        if (isExpiredOrCloseTo) {
            const bool isExpired = now >= certificateExpiryDateTime;
            qCWarning(KRDP) << "The TLS certificate" << (isExpired ? "expired" : "expires in") << (isExpired ? -certificateDays : certificateDays)
                            << (isExpired ? "day(s) ago:" : "day(s):") << (d->autogenerateCertificate ? "it will be regenerated" : "it should be replaced");
        }

        // Re-generate where appropriate, i.e. not effective (corrected system clock) or expired / close to expiry
        if (d->autogenerateCertificate && (!isEffective || isExpiredOrCloseTo) && !generateCertificate()) {
            qCWarning(KRDP) << "Failed to generate TLS certificate and key";
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

QSslCertificate Server::tlsCertificate() const
{
    return d->tlsCertificate.value;
}

void Server::setTlsCertificate(const std::filesystem::path &tlsCertificatePath)
{
    qCDebug(KRDP) << "Reading TLS certificate from" << tlsCertificatePath.string();

    d->tlsCertificate = {tlsCertificatePath, QSslCertificate()};

    QFile tlsCertificateFile(tlsCertificatePath);
    if (!tlsCertificateFile.open(QIODevice::ReadOnly)) {
        qCWarning(KRDP) << tlsCertificatePath.filename().string() << "could not be opened";
        return;
    }

    const QList<QSslCertificate> certificates = QSslCertificate::fromDevice(&tlsCertificateFile, QSsl::Pem);
    if (certificates.isEmpty() || certificates.first().isNull()) {
        qCWarning(KRDP) << tlsCertificatePath.filename().string() << "could not be read";
        return;
    }

    if (certificates.size() > 1) {
        qCWarning(KRDP) << tlsCertificatePath.filename().string() << "contains" << certificates.size() << "certificates; only the first will be used";
    }

    d->tlsCertificate.value = certificates.first();
}

QSslKey Server::tlsKey() const
{
    return d->tlsKey.value;
}

void Server::setTlsKey(const std::filesystem::path &tlsKeyPath)
{
    qCDebug(KRDP) << "Reading TLS key from" << tlsKeyPath.string();

    d->tlsKey = {tlsKeyPath, QSslKey()};

    QFile tlsKeyFile(tlsKeyPath);
    if (!tlsKeyFile.open(QIODevice::ReadOnly)) {
        qCWarning(KRDP) << tlsKeyPath.filename().string() << "could not be opened";
        return;
    }

    const QByteArray keyData = tlsKeyFile.readAll();

    QSslKey key;
    for (QSsl::KeyAlgorithm algorithm : {QSsl::Rsa, QSsl::Ec}) {
        key = QSslKey(keyData, algorithm, QSsl::Pem, QSsl::PrivateKey);

        if (!key.isNull()) {
            break;
        }
    }

    if (key.isNull()) {
        qCWarning(KRDP) << tlsKeyPath.filename().string() << "could not be read";
        return;
    }

    d->tlsKey.value = key;
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

bool Server::generateCertificate()
{
    if (d->tlsKey.path.empty() || d->tlsCertificate.path.empty()) {
        qCDebug(KRDP) << "Certificate generation failed because the key or certificate path was empty";
        return false;
    }

    const TlsHelpers::TlsResult<QSslKey> key = TlsHelpers::generatePrivateKey();
    if (key.value.isNull()) {
        qCDebug(KRDP) << "Certificate generation returned a null key";
        return false;
    }

    const TlsHelpers::TlsResult<QSslCertificate> certificate =
        TlsHelpers::generateSelfSignedCertificate(key, QStringLiteral("KRDP Auto-Generated Self-Signed Certificate"), -1, 365 * 5);
    if (certificate.value.isNull()) {
        qCDebug(KRDP) << "Certificate generation returned a null certificate";
        return false;
    }

    // Ensure parent directories exist
    std::error_code ec;
    std::filesystem::create_directories(d->tlsCertificate.path.parent_path(), ec);
    if (ec) {
        qCDebug(KRDP) << "The generated key could not be saved because the parent directories could not be created";
        return false;
    }
    std::filesystem::create_directories(d->tlsKey.path.parent_path(), ec);
    if (ec) {
        qCDebug(KRDP) << "The generated certificate could not be saved because the parent directories could not be created";
        return false;
    }

    // Write out key and certificate
    std::ofstream keyFile(d->tlsKey.path, std::ios::binary | std::ios::trunc);
    if (!keyFile) {
        qCDebug(KRDP) << "The generated key could not be saved because the file could not be opened";
        return false;
    }
    keyFile.write(key.pem.constData(), key.pem.size());

    std::ofstream certificateFile(d->tlsCertificate.path, std::ios::binary | std::ios::trunc);
    if (!certificateFile) {
        qCDebug(KRDP) << "The generated certificate could not be saved because the file could not be opened";
        return false;
    }
    certificateFile.write(certificate.pem.constData(), certificate.pem.size());

    // Use the generated key and certificate
    d->tlsKey.value = key.value;
    d->tlsCertificate.value = certificate.value;

    return true;
}

rdp_settings *Server::rdpSettings() const
{
    return d->settings;
}

#include "moc_Server.cpp"
