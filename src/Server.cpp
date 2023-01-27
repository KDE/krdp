// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Server.h"

#include <vector>

#include <QCoreApplication>

#include <freerdp/channels/channels.h>
#include <freerdp/freerdp.h>
#include <winpr/ssl.h>

#include "Session.h"

#include "krdp_logging.h"

using namespace KRdp;

class KRDP_NO_EXPORT Server::Private
{
public:
    std::vector<std::unique_ptr<Session>> sessions;
};

Server::Server(QObject *parent)
    : QTcpServer(parent)
    , d(std::make_unique<Private>())
{
    winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
}

Server::~Server()
{
}

void Server::start()
{
    if (!listen(QHostAddress::Any, 3389)) {
        qCWarning(KRDP) << "Unable to listen for connections on" << serverAddress() << serverPort();
        QCoreApplication::exit(1);
    }

    qCDebug(KRDP) << "Listening for connections on" << serverAddress() << serverPort();
}

void Server::stop()
{
    close();
}

void Server::incomingConnection(qintptr handle)
{
    qCDebug(KRDP) << "New incoming connection";

    auto session = std::make_unique<Session>(handle);
    d->sessions.push_back(std::move(session));
}
