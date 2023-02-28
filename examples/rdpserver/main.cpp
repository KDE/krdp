// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <QDebug>
#include <QGuiApplication>

#include "InputHandler.h"
#include "PortalSession.h"
#include "Server.h"
#include "Session.h"
#include "VideoStream.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};

    KRdp::Server server;

    server.setAddress(QHostAddress::Any);
    server.setPort(3389);
    server.setUserName(QStringLiteral("test"));
    server.setPassword(QStringLiteral("test"));
    server.setTlsCertificate(QStringLiteral("server.crt"));
    server.setTlsCertificateKey(QStringLiteral("server.key"));

    QObject::connect(&server, &KRdp::Server::newSession, [&server](KRdp::Session *newSession) {
        KRdp::PortalSession *portalSession = new KRdp::PortalSession(&server);

        QObject::connect(portalSession, &KRdp::PortalSession::frameReceived, newSession, [portalSession, newSession]() {
            newSession->videoStream()->sendFrame(portalSession->takeNextFrame());
        });

        QObject::connect(newSession->inputHandler(), &KRdp::InputHandler::inputEvent, portalSession, [portalSession](QEvent *event) {
            portalSession->sendEvent(event);
        });

        QObject::connect(newSession, &KRdp::Session::destroyed, portalSession, [portalSession]() {
            portalSession->deleteLater();
        });
    });

    server.start();

    return application.exec();
}
