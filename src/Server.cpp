// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Server.h"

#include <qevent.h>
#include <vector>

#include <QCoreApplication>
#include <QFile>

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
    if (!std::filesystem::exists(d->tlsCertificate) || !std::filesystem::exists(d->tlsCertificateKey)) {
        qCCritical(KRDP).nospace() << "A valid TLS certificate (" << QString::fromStdString(d->tlsCertificate.filename().string()) << ") and key ("
                                   << QString::fromStdString(d->tlsCertificateKey.filename().string()) << ") is required for the server to run!";
        return false;
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
    if (!waitForBytes(device, 11))
        return std::nullopt;

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


void Server::incomingConnection(qintptr handle)
{
    qDebug() << "incoming connection";
    {
        QFile tmpIoDevice;
        tmpIoDevice.open(handle, QIODeviceBase::ReadOnly);

        auto info = peekRdpRoutingInfo(&tmpIoDevice);

        if (!info)
        {
            qWarning() << "Failed to parse initial RDP packet";
            return;
        }

        if (info->token.isEmpty())
        {
            qDebug() << "Normal RDP connection";
        }
        else
        {
            qDebug() << "RDP routing token:" << info->token;
        }
    }

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

rdp_settings *Server::rdpSettings() const
{
    return d->settings;
}

#include "moc_Server.cpp"
