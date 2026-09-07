// SPDX-FileCopyrightText: 2026 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "SessionAdaptor.h"
#include "DaemonRdpConnection.h"
#include "DaemonServer.h"

#include <QDBusConnection>

using namespace Qt::StringLiterals;

namespace KRdp
{

SessionAdaptor::SessionAdaptor(DaemonServer *server)
    : m_server(server)
{
    QDBusConnection::sessionBus().registerObject(u"/Sessions"_s, this, QDBusConnection::ExportAllContents);
}

QDBusUnixFileDescriptor SessionAdaptor::GetNextSession()
{
}

}
