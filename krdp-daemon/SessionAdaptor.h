// SPDX-FileCopyrightText: 2026 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <QDBusUnixFileDescriptor>
#include <QObject>

namespace KRdp
{

class DaemonServer;

class SessionAdaptor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krdpd.Sessions")

public:
    explicit SessionAdaptor(DaemonServer *server);

public Q_SLOTS:
    QDBusUnixFileDescriptor GetNextSession();

private:
    DaemonServer *const m_server;
};

}
