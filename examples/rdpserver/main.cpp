// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <QDebug>
#include <QGuiApplication>

#include "Cursor.h"
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
    server.setTlsCertificate(std::filesystem::path("server.crt"));
    server.setTlsCertificateKey(std::filesystem::path("server.key"));

    QObject::connect(&server, &KRdp::Server::newSession, [&server](KRdp::Session *newSession) {
        auto portalSession = std::make_shared<KRdp::PortalSession>(&server);

        QObject::connect(portalSession.get(), &KRdp::PortalSession::frameReceived, newSession, [portalSession, newSession](const KRdp::VideoFrame &frame) {
            newSession->videoStream()->queueFrame(frame);
        });

        QObject::connect(portalSession.get(), &KRdp::PortalSession::cursorUpdate, newSession, [portalSession, newSession](const PipeWireCursor &cursor) {
            KRdp::Cursor::CursorUpdate update;
            update.hotspot = cursor.hotspot;
            update.image = cursor.texture;
            newSession->cursor()->update(update);
        });

        QObject::connect(newSession->videoStream(), &KRdp::VideoStream::enabledChanged, portalSession.get(), [newSession, portalSession]() {
            portalSession->setStreamingEnabled(newSession->videoStream()->enabled());
        });

        QObject::connect(newSession->inputHandler(), &KRdp::InputHandler::inputEvent, portalSession.get(), [portalSession](QEvent *event) {
            portalSession->sendEvent(event);
        });

    });

    if (!server.start()) {
        return 1;
    } else {
        return application.exec();
    }
}
