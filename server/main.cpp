// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <csignal>
#include <filesystem>

#include <QApplication>
#include <QCommandLineParser>
#include <QRegularExpression>

#include <KAboutData>
#include <KCrash>
#include <KSharedConfig>

#include <qt6keychain/keychain.h>

#include "Server.h"
#include "SessionController.h"
#include "krdp_version.h"
#include "krdpserversettings.h"

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QApplication application{argc, argv};
    application.setApplicationName(u"krdp-server"_s);
    application.setApplicationDisplayName(u"KRDP Server"_s);

    KAboutData about(u"krdp-server"_s, u"KRDP Server"_s, QStringLiteral(KRdp_VERSION_STRING));
    KAboutData::setApplicationData(about);

    KCrash::initialize();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"An RDP server that exposes the current desktop session over the RDP protocol.\nNote that a valid TLS certificate and key is needed. If not provided, a temporary certificate will be generated."_s);
    parser.addOptions({
        {{u"u"_s, u"username"_s}, u"The username to use for login"_s, u"username"_s},
        {{u"p"_s, u"password"_s}, u"The password to use for login. Requires username to be passed as well."_s, u"password"_s},
        {u"address"_s, u"The address to listen on for connections. Defaults to 0.0.0.0"_s, u"address"_s},
        {u"port"_s, u"The port to use for connections. Defaults to 3389."_s, u"port"_s, u"3389"_s},
        {u"certificate"_s, u"The TLS certificate file to use."_s, u"certificate"_s, u"server.crt"_s},
        {u"certificate-key"_s, u"The TLS certificate key to use."_s, u"certificate-key"_s, u"server.key"_s},
        {u"monitor"_s, u"The index of the monitor to use when streaming."_s, u"monitor"_s, u"-1"_s},
        {u"virtual-monitor"_s,
         u"Creates a new virtual output to connect to (WIDTHxHEIGHT@SCALE, e.g. 1920x1080@1). Incompatible with --monitor."_s,
         u"data"_s,
         u"1920x1080@1"_s},
        {u"quality"_s, u"Encoding quality of the stream, from 0 (lowest) to 100 (highest)"_s, u"quality"_s},
#ifdef WITH_PLASMA_SESSION
        {u"plasma"_s, u"Use Plasma protocols instead of XDP"_s},
#endif
    });
    about.setupCommandLine(&parser);
    parser.process(application);
    about.processCommandLine(&parser);

    signal(SIGINT, [](int) {
        QCoreApplication::exit(0);
    });

    signal(SIGTERM, [](int) {
        QCoreApplication::exit(0);
    });

    auto config = ServerConfig::self();

    auto parserValueWithDefault = [&parser](QAnyStringView option, auto defaultValue) {
        auto optionString = option.toString();
        if (parser.isSet(optionString)) {
            return QVariant(parser.value(optionString)).value<decltype(defaultValue)>();
        } else {
            return defaultValue;
        }
    };

    QHostAddress address = QHostAddress::Any;
    if (parser.isSet(u"address"_s)) {
        address = QHostAddress(parser.value(u"address"_s));
    }
    auto port = parserValueWithDefault(u"port", config->listenPort());
    auto certificate = std::filesystem::path(parserValueWithDefault(u"certificate", config->certificate()).toStdString());
    auto certificateKey = std::filesystem::path(parserValueWithDefault(u"certificate-key", config->certificateKey()).toStdString());

    KRdp::Server server(nullptr);

    server.setAddress(address);
    server.setPort(port);

    server.setTlsCertificate(certificate);
    server.setTlsCertificateKey(certificateKey);

    // Use parsed username/pw if set
    if (parser.isSet(u"username"_s)) {
        KRdp::User user;
        user.name = parser.value(u"username"_s);
        user.password = parser.value(u"password"_s);
        server.addUser(user);
    }
    // Otherwise use KCM username list
    else {
        server.setUsePAMAuthentication(config->systemUserEnabled());

        const auto users = config->users();
        for (const auto &userName : users) {
            const auto readJob = new QKeychain::ReadPasswordJob(QLatin1StringView("KRDP"));
            readJob->setKey(QLatin1StringView(userName.toLatin1()));
            QObject::connect(readJob, &QKeychain::ReadPasswordJob::finished, &server, [userName, readJob, &server]() {
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
        if (users.isEmpty() && !server.usePAMAuthentication()) {
            qWarning() << "No users configured for login. Either pass a username/password or configure users using kcm_krdp.";
            return -1;
        }
    }

    SessionController controller(&server, parser.isSet(u"plasma"_s) ? SessionController::SessionType::Plasma : SessionController::SessionType::Portal);
    if (parser.isSet(u"virtual-monitor"_s)) {
        const QString vmData = parser.value(u"virtual-monitor"_s);
        const QRegularExpression rx(uR"((\d+)x(\d+)@([\d.]+))"_s);
        const auto match = rx.match(vmData);
        if (!match.hasMatch()) {
            qWarning() << "failed to parse" << vmData << ".  Should be WIDTHxHEIGHT@SCALE";
            return 1;
        }
        controller.setVirtualMonitor({vmData, {match.capturedView(1).toInt(), match.capturedView(2).toInt()}, match.capturedView(3).toDouble()});
    } else {
        controller.setMonitorIndex(parser.isSet(u"monitor"_s) ? std::optional(parser.value(u"monitor"_s).toInt()) : std::nullopt);
    }
    controller.setQuality(parserValueWithDefault(u"quality", config->quality()));

    if (!server.start()) {
        return -1;
    }

    return application.exec();
}
