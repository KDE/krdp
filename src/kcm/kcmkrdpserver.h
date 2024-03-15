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

    Q_INVOKABLE QString password(const QString &user);
    Q_INVOKABLE void setPassword(const QString &user, const QString &password);

    void defaults() override;

public Q_SLOTS:
    void save() override;

Q_SIGNALS:
    void krdpServerSettingsChanged();

private:
    QString m_password;
};
