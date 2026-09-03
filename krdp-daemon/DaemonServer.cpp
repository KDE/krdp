// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "DaemonServer.h"

#include <fcntl.h>
#include <qevent.h>
#include <qprocess.h>
#include <qstandardpaths.h>
#include <qtcpsocket.h>
#include <vector>

#include <QCoreApplication>
#include <QFile>

#include <freerdp/channels/channels.h>
#include <freerdp/freerdp.h>
#include <winpr/ssl.h>

#include "DaemonRdpConnection.h"
#include "krdpd_logging.h"

using namespace KRdp;

class DaemonServer::Private
{
public:
    std::vector<std::unique_ptr<DaemonRdpConnection>> sessions;
    rdp_settings *settings = nullptr;

    QHostAddress address = QHostAddress::LocalHost;
    quint16 port = 3389;

    QList<User> users;
    bool usePamAuthentication = false;

    std::filesystem::path tlsCertificate;
    std::filesystem::path tlsCertificateKey;
};

DaemonServer::DaemonServer(QObject *parent)
    : QTcpServer(parent)
    , d(std::make_unique<Private>())
{
    winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
}

DaemonServer::~DaemonServer()
{
    stop();
}

bool DaemonServer::start()
{
    if (!std::filesystem::exists(d->tlsCertificate) || !std::filesystem::exists(d->tlsCertificateKey)) {
        qCCritical(KRDPD).nospace() << "A valid TLS certificate (" << QString::fromStdString(d->tlsCertificate.filename().string()) << ") and key ("
                                    << QString::fromStdString(d->tlsCertificateKey.filename().string()) << ") is required for the server to run!";
        return false;
    }

    if (!listen(d->address, d->port)) {
        // NOTE: We cannot use QTcpServer methods to get the server address and port because it won't initialize them if listen fails.
        qCCritical(KRDPD) << "Unable to listen for connections on" << d->address << d->port;
        return false;
    }

    // FreeRDP3 tries to use a global instance of the settings object when
    // initializing a new peer. However, it seems to fail at actually creating a
    // global default instance. So create one here and use that.
    d->settings = freerdp_settings_new(FREERDP_SETTINGS_SERVER_MODE);

    qCDebug(KRDPD) << "Listening for connections on" << serverAddress() << serverPort();
    return true;
}

void DaemonServer::stop()
{
    close();

    if (d->settings) {
        freerdp_settings_free(d->settings);
        d->settings = nullptr;
    }
}

QHostAddress DaemonServer::address() const
{
    return d->address;
}

void DaemonServer::setAddress(const QHostAddress &newAddress)
{
    if (newAddress == d->address) {
        return;
    }

    d->address = newAddress;
}

quint16 DaemonServer::port() const
{
    return d->port;
}

void DaemonServer::setPort(quint16 newPort)
{
    if (newPort == d->port) {
        return;
    }

    d->port = newPort;
}

QList<User> KRdp::DaemonServer::users() const
{
    return d->users;
}

void KRdp::DaemonServer::setUsers(const QList<User> &users)
{
    d->users = users;
}

void KRdp::DaemonServer::addUser(const User &user)
{
    d->users.append(user);
}

bool DaemonServer::usePAMAuthentication() const
{
    return d->usePamAuthentication;
}

void DaemonServer::setUsePAMAuthentication(bool usePAM)
{
    d->usePamAuthentication = usePAM;
}

std::filesystem::path DaemonServer::tlsCertificate() const
{
    return d->tlsCertificate;
}

void DaemonServer::setTlsCertificate(const std::filesystem::path &newTlsCertificate)
{
    if (newTlsCertificate == d->tlsCertificate) {
        return;
    }

    d->tlsCertificate = newTlsCertificate;
}

std::filesystem::path DaemonServer::tlsCertificateKey() const
{
    return d->tlsCertificateKey;
}

void DaemonServer::setTlsCertificateKey(const std::filesystem::path &newTlsCertificateKey)
{
    if (newTlsCertificateKey == d->tlsCertificateKey) {
        return;
    }

    d->tlsCertificateKey = newTlsCertificateKey;
}



static bool waitForBytes(QIODevice* device, qint64 count)
{
    while (device->bytesAvailable() < count)
    {
        if (!device->waitForReadyRead(-1))
            return false;
    }

    return true;
}


struct RdpRoutingInfo
{
    QString token;
};


static std::optional<RdpRoutingInfo>
peekRdpRoutingInfo(QIODevice* device)
{
    if (!device)
        return std::nullopt;

    /*
     * First get enough for:
     *
     * TPKT header                4 bytes
     * X.224 Connection Request   7 bytes
     *
     * Total                     11 bytes
     */
    if (!waitForBytes(device, 11)) {
        qDebug() << "failed to read 11 bytes";
        return std::nullopt;
    }

    QByteArray header = device->peek(11);

    if (header.size() < 11)
        return std::nullopt;

    const auto* p =
        reinterpret_cast<const quint8*>(header.constData());

    /*
     * TPKT:
     *
     *  0: version = 3
     *  1: reserved = 0
     *  2: length high
     *  3: length low
     */
    if (p[0] != 3 || p[1] != 0)
        return std::nullopt;

    const quint16 tpktLength =
        (static_cast<quint16>(p[2]) << 8) |
         static_cast<quint16>(p[3]);

    if (tpktLength < 11)
        return std::nullopt;

    /*
     * X.224 Connection Request TPDU.
     *
     * Offset 4: length indicator
     * Offset 5: TPDU code = 0xE0
     */
    if (p[5] != 0xE0)
        return std::nullopt;

    /*
     * Block until the whole initial TPKT has arrived.
     */
    if (!waitForBytes(device, tpktLength))
        return std::nullopt;

    QByteArray packet = device->peek(tpktLength);

    if (packet.size() != tpktLength)
        return std::nullopt;

    const auto* data =
        reinterpret_cast<const quint8*>(packet.constData());

    /*
     * Fixed X.224 Connection Request header ends at offset 11:
     *
     *  4      LI
     *  5      CR TPDU code
     *  6-7    destination reference
     *  8-9    source reference
     * 10      class/options
     * 11...   variable part
     */
    constexpr qsizetype variableOffset = 11;

    QByteArray variable =
        packet.mid(variableOffset);

    /*
     * If the variable part immediately starts with RDP_NEG_REQ,
     * there is no routing cookie/token.
     *
     * RDP_NEG_REQ:
     *
     *   BYTE   type      = 0x01
     *   BYTE   flags
     *   UINT16 length    = 0x0008 LE
     *   UINT32 protocols
     */
    if (variable.size() >= 8)
    {
        const auto* v =
            reinterpret_cast<const quint8*>(variable.constData());

        if (v[0] == 0x01 &&
            v[2] == 0x08 &&
            v[3] == 0x00)
        {
            return RdpRoutingInfo{};
        }
    }

    /*
     * Cookie / routing token is terminated by CRLF.
     */
    const qsizetype end = variable.indexOf("\r\n");

    if (end < 0)
        return RdpRoutingInfo{};

    RdpRoutingInfo result;

    result.token =
        QString::fromLatin1(variable.constData(), end);

    return result;
}


void DaemonServer::incomingConnection(qintptr handle)
{
    qDebug() << "incoming connection";
    {
        QTcpSocket tmpIoDevice;
        tmpIoDevice.setSocketDescriptor(handle, QTcpSocket::ConnectedState, QIODeviceBase::ReadWrite);

        auto info = peekRdpRoutingInfo(&tmpIoDevice);

        if (!info)
        {
            qWarning() << "Failed to parse initial RDP packet";
        } else if (info->token.isEmpty()) {
            qDebug() << "Normal RDP connection";
        } else {
            qDebug() << "RDP routing token:" << info->token;

            int newFd = dup(handle);

            const int originalFlags = fcntl(newFd, F_GETFD);
            if (originalFlags < 0) {
                qFatal("omgwtfbbq");
                return;
            }
            if (fcntl(newFd, F_SETFD, originalFlags & ~FD_CLOEXEC) < 0) {
                qFatal("omgwtfbbq");
            }

            qDebug() << "launching real krdp";

            QProcess *p = new QProcess(this);
            p->setProgram(QStandardPaths::findExecutable(QStringLiteral("krdpserver")));
            p->setArguments(
                {QStringLiteral("-u"), QStringLiteral("david"), QStringLiteral("-p"), QStringLiteral("foo"), QStringLiteral("--fd"), QString::number(newFd)});
            ::close(handle);

            qDebug() << newFd;

            connect(p, &QProcess::finished, []() {
                qDebug() << "krdp server closed";
            });

            p->setProcessChannelMode(QProcess::ForwardedErrorChannel);

            p->start();
        }
    }

    auto session = std::make_unique<DaemonRdpConnection>(this, handle);
    auto sessionPtr = session.get();
    // queued: signal comes from the run thread, and it keeps the erase below from destroying the sender mid-emission
    connect(
        sessionPtr,
        &DaemonRdpConnection::stateChanged,
        this,
        [this, sessionPtr](DaemonRdpConnection::State state) {
            if (state == DaemonRdpConnection::State::Closed) {
                auto itr = std::find_if(d->sessions.begin(), d->sessions.end(), [sessionPtr](auto &session) {
                    return session.get() == sessionPtr;
                });
                if (itr == d->sessions.end()) {
                    return;
                }
                // extracted before erasing: ~DaemonRdpConnection can spin a nested event loop (KScreen calls
                // from destroyed() handlers) that re-enters this vector, so destroy it once consistent again
                auto session = std::move(*itr);
                d->sessions.erase(itr);
            }
        },
        Qt::QueuedConnection);
    d->sessions.push_back(std::move(session));
    Q_EMIT newConnectionCreated(sessionPtr);
}

rdp_settings *DaemonServer::rdpSettings() const
{
    return d->settings;
}

#include "moc_DaemonServer.cpp"
