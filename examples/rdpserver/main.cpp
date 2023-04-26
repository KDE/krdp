// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <QCommandLineParser>
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
    application.setApplicationName(u"krdp-server"_qs);
    application.setApplicationDisplayName(u"KRDP Example Server"_qs);

    QCommandLineParser parser;
    parser.addOptions({
        {{u"u"_qs, u"username"_qs}, u"The username to use for login. Required."_qs, u"username"_qs},
        {{u"p"_qs, u"password"_qs}, u"The password to use for login. Required."_qs, u"password"_qs},
        {u"port"_qs, u"The port to use for connections. Defaults to 3389."_qs, u"port"_qs, u"3389"_qs},
        {u"certificate"_qs, u"The TLS certificate file to use. Defaults to 'server.crt' in the current directory."_qs, u"certificate"_qs, u"server.crt"_qs},
        {u"certificate-key"_qs,
         u"The TLS certificate key to use. Defaults to 'server.key' in the current directory."_qs,
         u"certificate-key"_qs,
         u"server.key"_qs},
    });
    parser.addHelpOption();
    parser.process(application);

    if (!parser.isSet(u"username"_qs) || !parser.isSet(u"password"_qs)) {
        qCritical() << "A username and password needs to be provided.";
        parser.showHelp(1);
    }

    auto certificate = std::filesystem::path(parser.value(u"certificate"_qs).toStdString());
    auto certificateKey = std::filesystem::path(parser.value(u"certificate-key"_qs).toStdString());

    if (!std::filesystem::exists(certificate)) {
        qCritical() << "The specified certificate file" << certificate.string() << "does not exist. A valid TLS certificate file is required.";
        parser.showHelp(2);
    }

    if (!std::filesystem::exists(certificateKey)) {
        qCritical() << "The specified certificate key" << certificateKey.string() << "does not exist. A valid TLS certificate key is required.";
        parser.showHelp(3);
    }

    KRdp::Server server;

    server.setAddress(QHostAddress::Any);
    server.setPort(parser.value(u"port"_qs).toInt());
    server.setUserName(parser.value(u"username"_qs));
    server.setPassword(parser.value(u"password"_qs));
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

        QObject::connect(newSession->videoStream(), &KRdp::VideoStream::requestedFrameRateChanged, portalSession.get(), [newSession, portalSession]() {
            portalSession->setVideoFrameRate(newSession->videoStream()->requestedFrameRate());
        });

        QObject::connect(newSession->inputHandler(), &KRdp::InputHandler::inputEvent, portalSession.get(), [portalSession](QEvent *event) {
            portalSession->sendEvent(event);
        });

    });

    if (!server.start()) {
        return -1;
    } else {
        return application.exec();
    }
}
