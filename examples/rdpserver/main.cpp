// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <csignal>
#include <filesystem>

#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QProcess>

#include "Cursor.h"
#include "InputHandler.h"
#include "PortalSession.h"
#include "RdpSession.h"
#include "Server.h"
#include "VideoStream.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};
    application.setApplicationName(u"krdp-server"_qs);
    application.setApplicationDisplayName(u"KRDP Example Server"_qs);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"A basic RDP server that exposes the current desktop session over the RDP protocol.\nNote that a valid TLS certificate and key is needed. If not provided, a temporary certificate will be generated."_qs);
    parser.addHelpOption();
    parser.addOptions({
        {{u"u"_qs, u"username"_qs}, u"The username to use for login. Required."_qs, u"username"_qs},
        {{u"p"_qs, u"password"_qs}, u"The password to use for login. Required."_qs, u"password"_qs},
        {u"port"_qs, u"The port to use for connections. Defaults to 3389."_qs, u"port"_qs, u"3389"_qs},
        {u"certificate"_qs, u"The TLS certificate file to use."_qs, u"certificate"_qs, u"server.crt"_qs},
        {u"certificate-key"_qs, u"The TLS certificate key to use."_qs, u"certificate-key"_qs, u"server.key"_qs},
        {u"monitor"_qs, u"The index of the monitor to use when streaming."_qs, u"monitor"_qs, u"-1"_qs},
        {u"quality"_qs, u"Encoding quality of the stream, from 0 (lowest) to 100 (highest)"_qs, u"quality"_qs},
    });
    parser.process(application);

    if (!parser.isSet(u"username"_qs) || !parser.isSet(u"password"_qs)) {
        qCritical() << "Error: A username and password needs to be provided.";
        parser.showHelp(1);
    }

    signal(SIGINT, [](int) {
        QCoreApplication::exit(0);
    });

    auto certificate = std::filesystem::path(parser.value(u"certificate"_qs).toStdString());
    auto certificateKey = std::filesystem::path(parser.value(u"certificate-key"_qs).toStdString());
    bool certificateGenerated = false;

    if (!std::filesystem::exists(certificate) || !std::filesystem::exists(certificateKey)) {
        qWarning() << "Could not find certificate and certificate key, generating temporary certificate...";
        QProcess sslProcess;
        sslProcess.start(u"openssl"_qs,
                         {
                             u"req"_qs,
                             u"-nodes"_qs,
                             u"-new"_qs,
                             u"-x509"_qs,
                             u"-keyout"_qs,
                             u"/tmp/krdp.key"_qs,
                             u"-out"_qs,
                             u"/tmp/krdp.crt"_qs,
                             u"-days"_qs,
                             u"1"_qs,
                             u"-batch"_qs,
                         });
        sslProcess.waitForFinished();

        certificate = std::filesystem::path("/tmp/krdp.crt");
        certificateKey = std::filesystem::path("/tmp/krdp.key");
        certificateGenerated = true;

        if (!std::filesystem::exists(certificate) || !std::filesystem::exists(certificateKey)) {
            qCritical() << "Could not generate a certificate and no certificate provided. A valid TLS certificate and key should be provided.";
            parser.showHelp(2);
        } else {
            qWarning() << "Temporary certificate generated; ready to accept connections.";
        }
    }

    quint32 monitorIndex = parser.value(u"monitor"_qs).toInt();

    KRdp::Server server;

    server.setAddress(QHostAddress::Any);
    server.setPort(parser.value(u"port"_qs).toInt());
    server.setUserName(parser.value(u"username"_qs));
    server.setPassword(parser.value(u"password"_qs));
    server.setTlsCertificate(certificate);
    server.setTlsCertificateKey(certificateKey);

    auto portalSession = std::make_shared<KRdp::PortalSession>(&server);
    portalSession->setActiveStream(monitorIndex);
    if (parser.isSet(u"quality"_qs)) {
        portalSession->setVideoQuality(parser.value(u"quality"_qs).toUShort());
    }

    QObject::connect(portalSession, &KRdp::AbstractSession::error, []() {
        QCoreApplication::exit(-1);
    });

    QObject::connect(&server, &KRdp::Server::newSession, [&portalSession](KRdp::RdpSession *newSession) {
        QObject::connect(portalSession, &KRdp::AbstractSession::frameReceived, newSession, [newSession](const KRdp::VideoFrame &frame) {
            newSession->videoStream()->queueFrame(frame);
        });

        QObject::connect(portalSession, &KRdp::AbstractSession::cursorUpdate, newSession, [newSession](const PipeWireCursor &cursor) {
            KRdp::Cursor::CursorUpdate update;
            update.hotspot = cursor.hotspot;
            update.image = cursor.texture;
            newSession->cursor()->update(update);
        });

        QObject::connect(newSession->videoStream(), &KRdp::VideoStream::enabledChanged, portalSession, [newSession, portalSession]() {
            if (newSession->videoStream()->enabled()) {
                portalSession->requestStreamingEnable(newSession->videoStream());
            } else {
                portalSession->requestStreamingDisable(newSession->videoStream());
            }
        });

        QObject::connect(newSession->videoStream(), &KRdp::VideoStream::requestedFrameRateChanged, portalSession, [newSession, portalSession]() {
            portalSession->setVideoFrameRate(newSession->videoStream()->requestedFrameRate());
        });

        QObject::connect(newSession->inputHandler(), &KRdp::InputHandler::inputEvent, portalSession, [portalSession](QEvent *event) {
            portalSession->sendEvent(event);
        });
    });

    if (!server.start()) {
        return -1;
    }

    auto result = application.exec();
    if (certificateGenerated) {
        std::filesystem::remove(certificate);
        std::filesystem::remove(certificateKey);
    }
    return result;
}
