// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <KQuickConfigModule>

class KRDPModule : public KQuickConfigModule
{
    Q_OBJECT
public:
    Q_PROPERTY(QString username READ getUsername WRITE setUsername)
    Q_PROPERTY(QString password READ getPassword WRITE setPassword)
    Q_PROPERTY(int port READ getPort WRITE setPort)
    Q_PROPERTY(QString certPath READ getCertPath WRITE setCertPath)
    Q_PROPERTY(QString certKeyPath READ getCertKeyPath WRITE setCertKeyPath)
    Q_PROPERTY(int quality READ getQuality WRITE setQuality)

    KRDPModule(QObject *parent, const KPluginMetaData &data, const QVariantList &args);

    Q_INVOKABLE QString toLocalFile(const QUrl url);

    void load() override;
    void save() override;

    QString getUsername();
    QString getPassword();
    int getPort();
    QString getCertPath();
    QString getCertKeyPath();
    int getQuality();

    void setUsername(const QString &username);
    void setPassword(const QString &password);
    void setPort(const int &port);
    void setCertPath(const QString &certPath);
    void setCertKeyPath(const QString &certKeyPath);
    void setQuality(const int &quality);

private:
    QString m_username;
    QString m_password;
    int m_port;
    QString m_certPath;
    QString m_certKeyPath;
    int m_quality;
    QLatin1String m_settingsFile = QLatin1String("krdprc");
};
