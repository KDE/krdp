// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <csignal>
#include <filesystem>

#include <QApplication>
#include <QCommandLineParser>
#include <QGuiApplication>

#include <KSharedConfig>
#include <qt6keychain/keychain.h>

#include "Server.h"

#include "SessionController.h"
#include "krdpserversettings.h"
#include <KStatusNotifierItem>
#include <QObject>
#include <QWindow>

int main(int argc, char **argv)
{
    QApplication application{argc, argv};
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

    signal(SIGTERM, [](int) {
        QCoreApplication::exit(0);
    });

    signal(SIGKILL, [](int) {
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

    auto sni(new KStatusNotifierItem(u"krdpserver"_qs));
    sni->setTitle(u"RDP Server"_qs);
    sni->setIconByName(u"preferences-system-network-remote"_qs);
    sni->setStatus(KStatusNotifierItem::Passive);

    KRdp::Server server(nullptr, sni);
    server.setAddress(address);
    server.setPort(port);

    server.setTlsCertificate(certificate);
    server.setTlsCertificateKey(certificateKey);

    // Use parsed username/pw if set
    if (parser.isSet(u"username"_qs)) {
        KRdp::User user;
        user.name = parser.value(u"username"_qs);
        user.password = parser.value(u"password"_qs);
        server.addUser(user);
    }
    // Otherwise use KCM username list
    else {
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
    }

    SessionController controller(&server);
    controller.setUsePlasmaSession(parser.isSet(u"plasma"_qs));
    controller.setMonitorIndex(parser.isSet(u"monitor"_qs) ? std::optional(parser.value(u"monitor"_qs).toInt()) : std::nullopt);
    controller.setQuality(parser.isSet(u"quality"_qs) ? std::optional(parser.value(u"quality"_qs).toInt()) : std::nullopt);

    if (!server.start()) {
        return -1;
    }

    return application.exec();
}
