// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <KQuickConfigModule>

class KRDPModule : public KQuickConfigModule
{
    Q_OBJECT
public:
    Q_PROPERTY(QString username READ getUsername WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(QString password READ getPassword WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(int port READ getPort WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QString certPath READ getCertPath WRITE setCertPath NOTIFY certPathChanged)
    Q_PROPERTY(QString certKeyPath READ getCertKeyPath WRITE setCertKeyPath NOTIFY certKeyPathChanged)
    Q_PROPERTY(int quality READ getQuality WRITE setQuality NOTIFY qualityChanged)

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

Q_SIGNALS:
    void usernameChanged();
    void passwordChanged();
    void portChanged();
    void certPathChanged();
    void certKeyPathChanged();
    void qualityChanged();

private:
    QString m_username;
    QString m_password;
    int m_port;
    QString m_certPath;
    QString m_certKeyPath;
    int m_quality;
    QLatin1String m_settingsFile = QLatin1String("krdprc");
};
