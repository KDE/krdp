// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <KQuickManagedConfigModule>
#include <qt6keychain/keychain.h>

class KRDPServerConfigImpl;

class KRDPServerConfig : public KQuickManagedConfigModule
{
    Q_OBJECT
public:
    explicit KRDPServerConfig(QObject *parent, const KPluginMetaData &data);
    ~KRDPServerConfig() override;

    Q_INVOKABLE QString toLocalFile(const QUrl url);

    QString password(const QString &user);
    void setPassword(const QString &user, const QString &password);

private:
    QString m_password;
};
