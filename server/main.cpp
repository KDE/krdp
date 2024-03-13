// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <csignal>
#include <filesystem>

#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QProcess>

#include <KDBusService>
#include <KSharedConfig>

#include "Cursor.h"
#include "InputHandler.h"
#include "PortalSession.h"
#include "RdpConnection.h"
#include "Server.h"
#include "VideoStream.h"
#ifdef WITH_PLASMA_SESSION
#include "PlasmaScreencastV1Session.h"
#endif

#include "krdpserverrc.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};
    application.setApplicationName(u"krdp-server"_qs);
    application.setApplicationDisplayName(u"KRDP Server"_qs);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"An RDP server that exposes the current desktop session over the RDP protocol.\nNote that a valid TLS certificate and key is needed. If not provided, a temporary certificate will be generated."_qs);
    parser.addHelpOption();
    parser.addOptions({
        {{u"u"_qs, u"username"_qs}, u"The username to use for login"_qs, u"username"_qs},
        {{u"p"_qs, u"password"_qs}, u"The password to use for login. Requires username to be passed as well."_qs, u"password"_qs},
        {u"address"_qs, u"The address to listen on for connections. Defaults to 0.0.0.0"_qs, u"address"_qs},
        {u"port"_qs, u"The port to use for connections. Defaults to 3389."_qs, u"port"_qs, u"3389"_qs},
        {u"certificate"_qs, u"The TLS certificate file to use."_qs, u"certificate"_qs, u"server.crt"_qs},
        {u"certificate-key"_qs, u"The TLS certificate key to use."_qs, u"certificate-key"_qs, u"server.key"_qs},
        {u"monitor"_qs, u"The index of the monitor to use when streaming."_qs, u"monitor"_qs, u"-1"_qs},
        {u"quality"_qs, u"Encoding quality of the stream, from 0 (lowest) to 100 (highest)"_qs, u"quality"_qs},
#ifdef WITH_PLASMA_SESSION
        {u"plasma"_qs, u"Use Plasma protocols instead of XDP"_qs},
#endif
    });
    parser.process(application);

    signal(SIGINT, [](int) {
        QCoreApplication::exit(0);
    });

    auto config = ServerConfig::self();

    auto parserValueWithDefault = [&parser, config](QAnyStringView option, auto defaultValue) {
        auto optionString = option.toString();
        if (parser.isSet(optionString)) {
            return QVariant(parser.value(optionString)).value<decltype(defaultValue)>();
        } else {
            return defaultValue;
        }
    };

    auto address = QHostAddress(parserValueWithDefault(u"address", config->listenAddress()));
    auto port = parserValueWithDefault(u"port", config->listenPort());
    auto certificate = std::filesystem::path(parserValueWithDefault(u"certificate", config->certificate()).toStdString());
    auto certificateKey = std::filesystem::path(parserValueWithDefault(u"certificate-key", config->certificateKey()).toStdString());

    // auto certificate = std::filesystem::path(parser.value(u"certificate"_qs).toStdString());
    // auto certificateKey = std::filesystem::path(parser.value(u"certificate-key"_qs).toStdString());
    // bool certificateGenerated = false;
    //
    // if (!std::filesystem::exists(certificate) || !std::filesystem::exists(certificateKey)) {
    //     qWarning() << "Could not find certificate and certificate key, generating temporary certificate...";
    //     QProcess sslProcess;
    //     sslProcess.start(u"openssl"_qs,
    //                      {
    //                          u"req"_qs,
    //                          u"-nodes"_qs,
    //                          u"-new"_qs,
    //                          u"-x509"_qs,
    //                          u"-keyout"_qs,
    //                          u"/tmp/krdp.key"_qs,
    //                          u"-out"_qs,
    //                          u"/tmp/krdp.crt"_qs,
    //                          u"-days"_qs,
    //                          u"1"_qs,
    //                          u"-batch"_qs,
    //                      });
    //     sslProcess.waitForFinished();
    //
    //     certificate = std::filesystem::path("/tmp/krdp.crt");
    //     certificateKey = std::filesystem::path("/tmp/krdp.key");
    //     certificateGenerated = true;
    //
    //     if (!std::filesystem::exists(certificate) || !std::filesystem::exists(certificateKey)) {
    //         qCritical() << "Could not generate a certificate and no certificate provided. A valid TLS certificate and key should be provided.";
    //         parser.showHelp(2);
    //     } else {
    //         qWarning() << "Temporary certificate generated; ready to accept connections.";
    //     }
    // }

    quint32 monitorIndex = parser.value(u"monitor"_qs).toInt();

    KRdp::Server server;

    server.setAddress(address);
    server.setPort(port);
    server.setTlsCertificate(certificate);
    server.setTlsCertificateKey(certificateKey);

    if (parser.isSet(u"username"_qs)) {
        // server.setUserMode(KRdp::Server::UserMode::LocalNoWallet);
        KRdp::User user;
        user.name = parser.value(u"username"_qs);
        user.password = parser.value(u"password"_qs);
        server.addUser(user);
    }

    std::unique_ptr<KRdp::AbstractSession> session;
#ifdef WITH_PLASMA_SESSION
    if (parser.isSet(u"plasma"_qs)) {
        session = std::unique_ptr<KRdp::AbstractSession>(new KRdp::PlasmaScreencastV1Session(&server));
    } else
#endif
    {
        session = std::unique_ptr<KRdp::AbstractSession>(new KRdp::PortalSession(&server));
    }
    KRdp::AbstractSession *portalSession = session.get();
    portalSession->setActiveStream(monitorIndex);
    if (parser.isSet(u"quality"_qs)) {
        portalSession->setVideoQuality(parser.value(u"quality"_qs).toUShort());
    }

    QObject::connect(portalSession, &KRdp::AbstractSession::error, []() {
        QCoreApplication::exit(-1);
    });

    QObject::connect(&server, &KRdp::Server::newConnection, [&portalSession](KRdp::RdpConnection *newConnection) {
        QObject::connect(portalSession, &KRdp::AbstractSession::frameReceived, newConnection, [newConnection](const KRdp::VideoFrame &frame) {
            newConnection->videoStream()->queueFrame(frame);
        });

        QObject::connect(portalSession, &KRdp::AbstractSession::cursorUpdate, newConnection, [newConnection](const PipeWireCursor &cursor) {
            KRdp::Cursor::CursorUpdate update;
            update.hotspot = cursor.hotspot;
            update.image = cursor.texture;
            newConnection->cursor()->update(update);
        });

        QObject::connect(newConnection->videoStream(), &KRdp::VideoStream::enabledChanged, portalSession, [newConnection, portalSession]() {
            if (newConnection->videoStream()->enabled()) {
                portalSession->requestStreamingEnable(newConnection->videoStream());
            } else {
                portalSession->requestStreamingDisable(newConnection->videoStream());
            }
        });

        QObject::connect(newConnection->videoStream(), &KRdp::VideoStream::requestedFrameRateChanged, portalSession, [newConnection, portalSession]() {
            portalSession->setVideoFrameRate(newConnection->videoStream()->requestedFrameRate());
        });

        QObject::connect(newConnection->inputHandler(), &KRdp::InputHandler::inputEvent, portalSession, [portalSession](QEvent *event) {
            portalSession->sendEvent(event);
        });
    });

    if (!server.start()) {
        return -1;
    }

    // KDBusService::

    auto result = application.exec();
    // if (certificateGenerated) {
    //     std::filesystem::remove(certificate);
    //     std::filesystem::remove(certificateKey);
    // }
    return result;
}
