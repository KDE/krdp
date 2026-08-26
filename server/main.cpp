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
#include <KSignalHandler>

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
    about.setDesktopFileName(u"org.kde.krdpserver"_s);
    KAboutData::setApplicationData(about);

    KCrash::initialize();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"An RDP server that exposes the current desktop session over the RDP protocol.\nNote that a valid TLS certificate and key is needed. If not provided, a temporary certificate will be generated."_s);
    parser.addOptions({{{u"u"_s, u"username"_s}, u"The username to use for login"_s, u"username"_s},
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
                       {u"fd"_s, u"File descriptor for existing connection"_s}});
    about.setupCommandLine(&parser);
    parser.process(application);
    about.processCommandLine(&parser);

    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGTERM);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &application, [](int) {
        QCoreApplication::exit(0);
    });

    int fd = parser.value(u"fd"_s).toInt();
    if (fd <= 0) {
        qFatal("no fd provided");
    }

    KRdp::RdpConnection connection(nullptr, fd);

    return application.exec();
}
