// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "krdpserversettings.h"
#include <KQuickManagedConfigModule>
#include <qt6keychain/keychain.h>

class KRDPServerConfigImpl;

class KRDPServerConfig : public KQuickManagedConfigModule
{
    Q_OBJECT
public:
    explicit KRDPServerConfig(QObject *parent, const KPluginMetaData &data);
    ~KRDPServerConfig() override;

    Q_INVOKABLE QString toLocalFile(const QUrl &url);

    Q_INVOKABLE void modifyUser(const QString &oldUsername, const QString &newUsername, const QString &newPassword);
    Q_INVOKABLE void addUser(const QString &username, const QString &password);
    Q_INVOKABLE void deleteUser(const QString &username);
    Q_INVOKABLE bool userExists(const QString &username);

    Q_INVOKABLE void readPasswordFromWallet(const QString &user);
    void writePasswordToWallet(const QString &user, const QString &password);
    void deletePasswordFromWallet(const QString &user);

    Q_INVOKABLE bool isH264Supported();
    Q_INVOKABLE QString listenAddress();
    Q_INVOKABLE void toggleAutoconnect(const bool enabled);
    Q_INVOKABLE void toggleServer(const bool enabled);

    Q_INVOKABLE void generateCertificate();
    Q_INVOKABLE bool isServerRunning();
    Q_INVOKABLE KRDPServerSettings *settings() const
    {
        return m_serverSettings;
    };

public Q_SLOTS:
    void save() override;
    void defaults() override;

Q_SIGNALS:
    void krdpServerSettingsChanged();
    void generateCertificateSucceeded();
    void generateCertificateFailed();
    void passwordLoaded(const QString &user, const QString &password);
    void keychainError(const QString &errorText);

private:
    KRDPServerSettings *m_serverSettings;
};
