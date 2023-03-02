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
        auto portalSession = std::make_shared<KRdp::PortalSession>(&server);

        QObject::connect(portalSession.get(), &KRdp::PortalSession::frameReceived, newSession, [portalSession, newSession](const KRdp::VideoFrame &frame) {
            newSession->videoStream()->queueFrame(frame);
        });

        QObject::connect(newSession->inputHandler(), &KRdp::InputHandler::inputEvent, portalSession.get(), [portalSession](QEvent *event) {
            portalSession->sendEvent(event);
        });

    });

    server.start();

    return application.exec();
}
