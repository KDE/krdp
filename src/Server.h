// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>

#include <QTcpServer>

#include <freerdp/settings.h>

#include "krdp_export.h"

namespace KRdp
{

class KRDP_EXPORT Server : public QTcpServer
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;

    void start();
    void stop();

    QHostAddress address() const;
    void setAddress(const QHostAddress &newAddress);

    quint16 port() const;
    void setPort(quint16 newPort);

    QString userName() const;
    void setUserName(const QString &userName);

    QString password() const;
    void setPassword(const QString &password);

    QString tlsCertificate() const;
    void setTlsCertificate(const QString &newTlsCertificate);

    QString tlsCertificateKey() const;
    void setTlsCertificateKey(const QString &newTlsCertificateKey);

protected:
    void incomingConnection(qintptr handle) override;

private:
    friend class Session;

    rdp_settings *rdpSettings() const;

    class Private;
    const std::unique_ptr<Private> d;
};

}
