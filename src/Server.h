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

class Session;

class KRDP_EXPORT Server : public QTcpServer
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;

    bool start();
    void stop();

    QHostAddress address() const;
    void setAddress(const QHostAddress &newAddress);

    quint16 port() const;
    void setPort(quint16 newPort);

    QString userName() const;
    void setUserName(const QString &userName);

    QString password() const;
    void setPassword(const QString &password);

    std::filesystem::path tlsCertificate() const;
    void setTlsCertificate(const std::filesystem::path &newTlsCertificate);

    std::filesystem::path tlsCertificateKey() const;
    void setTlsCertificateKey(const std::filesystem::path &newTlsCertificateKey);

    Q_SIGNAL void newSession(Session *session);

protected:
    void incomingConnection(qintptr handle) override;

private:
    friend class Session;

    rdp_settings *rdpSettings() const;

    class Private;
    const std::unique_ptr<Private> d;
};

}
