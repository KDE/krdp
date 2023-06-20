// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <csignal>

#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QTimer>
#include <QUrl>

#include "PortalSession.h"
#include "VideoStream.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {u"quit-after"_qs, u"Quit after running for this amount of seconds"_qs, u"seconds"_qs},
        {u"monitor"_qs, u"Index of the monitor to display."_qs, u"monitor"_qs, u"-1"_qs},
        {u"quality"_qs, u"Encoding quality of the stream, from 0 (lowest) to 100 (highest)"_qs, u"quality"_qs},
    });
    parser.process(application);

    KRdp::PortalSession session{nullptr};
    session.requestStreamingEnable(&application);
    session.setActiveStream(parser.value(u"monitor"_qs).toInt());
    if (parser.isSet(u"quality"_qs)) {
        session.setVideoQuality(parser.value(u"quality"_qs).toUShort());
    }

    signal(SIGINT, [](int) {
        QCoreApplication::exit(0);
    });

    signal(SIGUSR1, [](int) {
        QCoreApplication::exit(0);
    });

    QFile file{u"stream.raw"_qs};
    if (!file.open(QFile::WriteOnly)) {
        qDebug() << "Failed opening stream.raw";
        return -1;
    }

    QObject::connect(&session, &KRdp::PortalSession::frameReceived, &session, [&file](const KRdp::VideoFrame &frame) {
        file.write(frame.data);
    });

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &application, &QCoreApplication::quit);
    if (parser.isSet(u"quit-after"_qs)) {
        QObject::connect(&session, &KRdp::PortalSession::started, &timer, qOverload<>(&QTimer::start));
        timer.setInterval(parser.value(u"quit-after"_qs).toInt() * 1000);
    }

    auto result = application.exec();

    file.close();

    return result;
}
