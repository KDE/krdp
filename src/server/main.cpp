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
#include <qt6/QtGui/qguiapplication.h>

#include "Cursor.h"
#include "InputHandler.h"
#include "PortalSession.h"
#include "RdpConnection.h"
#include "Server.h"
#include "VideoStream.h"
#ifdef WITH_PLASMA_SESSION
#include "PlasmaScreencastV1Session.h"
#endif
#include <qt6keychain/keychain.h>

#include "krdpserversettings.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};
    application.setApplicationName(u"krdp-server"_qs);
    application.setApplicationDisplayName(u"KRDP Server"_qs);

    signal(SIGINT, [](int) {
        QCoreApplication::exit(0);
    });

    auto config = new KRDPServerSettings();

    auto address = QHostAddress(config->listenAddress());
    auto port = config->listenPort();
    auto certificate = std::filesystem::path(config->certificate().toStdString());
    auto certificateKey = std::filesystem::path(config->certificateKey().toStdString());

    KRdp::Server server;

    server.setAddress(address);
    server.setPort(port);

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
        } else {
            qWarning() << "Temporary certificate generated; ready to accept connections.";
        }
    }

    server.setTlsCertificate(certificate);
    server.setTlsCertificateKey(certificateKey);

    for (auto userName : config->users()) {
        const auto readJob = new QKeychain::ReadPasswordJob(QLatin1StringView("KRDP"));
        readJob->setKey(QLatin1StringView(userName.toLatin1()));
        QObject::connect(readJob, &QKeychain::ReadPasswordJob::finished, [userName, readJob, &server]() {
            KRdp::User user;
            if (readJob->error() != QKeychain::Error::NoError) {
                qWarning() << "requestPassword: Failed to read password of " << userName << " because of error: " << readJob->error();
                return;
            }
            user.name = userName;
            user.password = readJob->textData();
            server.addUser(user);
        });
        readJob->start();
    }

    std::unique_ptr<KRdp::AbstractSession> session;
    session = std::unique_ptr<KRdp::AbstractSession>(new KRdp::PortalSession(&server));

    KRdp::AbstractSession *portalSession = session.get();
    portalSession->setActiveStream(0);
    portalSession->setVideoQuality(config->quality());

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

    auto result = application.exec();

    if (certificateGenerated) {
        std::filesystem::remove(certificate);
        std::filesystem::remove(certificateKey);
    }

    return result;
}
