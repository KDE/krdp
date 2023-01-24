// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Server.h"

using namespace KRdp;

class KRDP_NO_EXPORT Server::Private
{
public:
};

Server::Server(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

Server::~Server()
{
}
