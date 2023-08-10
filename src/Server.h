// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <filesystem>
#include <memory>

#include <QTcpServer>

#include <freerdp/settings.h>

#include "krdp_export.h"

namespace KRdp
{

class RdpSession;

/**
 * Core RDP server class.
 *
 * This class listens for TCP connections and creates a new @c Session for each
 * incoming connection. It takes care of basic system initialisation. It also
 * stores connection and security settings.
 */
class KRDP_EXPORT Server : public QTcpServer
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;

    /**
     * Start listening for incoming connections.
     *
     * Note that `address` and `port` should be set before calling this,
     * changing them after the server has started listening has no effect.
     */
    bool start();
    /**
     * Stop listening for incoming connections.
     */
    void stop();

    /**
     * The host address to listen on.
     *
     * Set this to an appropriate address for the server to listen on. Common
     * options are `0.0.0.0` to listen on all interfaces and accept all incoming
     * connections or `127.0.0.1` to only listen on the loopback interface and
     * only allow local connections.
     *
     * By default the address is set to QHostAddress::LocalHost
     */
    QHostAddress address() const;
    void setAddress(const QHostAddress &newAddress);

    /**
     * The port to listen on.
     *
     * By default this is set to 3389, which is the standard port used for RDP.
     */
    quint16 port() const;
    void setPort(quint16 newPort);

    /**
     * The username needed to log in to the server.
     *
     * Note that if this is changed while the server is running, it will only
     * take effect for new sessions.
     */
    QString userName() const;
    void setUserName(const QString &userName);

    /**
     * The password needed to log in to the server.
     *
     * Note that if this is changed while the server is running, it will only
     * take effect for new sessions.
     */
    QString password() const;
    void setPassword(const QString &password);

    /**
     * The path of a certificate file to use for encrypting communications.
     *
     * This is required to be set to a valid file, as the RDP login process only
     * works over an encrypted connection.
     */
    std::filesystem::path tlsCertificate() const;
    void setTlsCertificate(const std::filesystem::path &newTlsCertificate);

    /**
     * The path of a certificate key to use for encrypting communications.
     *
     * This is required to be set to a valid file, as the RDP login process only
     * works over an encrypted connection.
     */
    std::filesystem::path tlsCertificateKey() const;
    void setTlsCertificateKey(const std::filesystem::path &newTlsCertificateKey);

    /**
     * Emitted whenever a new session is started.
     *
     * \param session The new session that was just started.
     */
    Q_SIGNAL void newSession(RdpSession *session);

protected:
    /**
     * Overridden from QTcpServer
     */
    void incomingConnection(qintptr handle) override;

private:
    friend class RdpSession;
    rdp_settings *rdpSettings() const;

    class Private;
    const std::unique_ptr<Private> d;
};

}
