// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <KConfigGroup>
#include <KQuickConfigModule>
#include <KSharedConfig>
#include <qt6keychain/keychain.h>

class KRDPServerConfig : public KQuickConfigModule
{
    Q_OBJECT
public:
    KRDPServerConfig(QObject *parent, const KPluginMetaData &data);

    Q_INVOKABLE QString toLocalFile(const QUrl url);

    QString password();
    void setPassword(const QString &password);

private:
    KSharedConfig::Ptr m_config;
    KConfigGroup m_configGroup;
    QString m_password;
};
