// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include <csignal>

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QUrl>

#include "PortalSession.h"
#include "VideoStream.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};

    KRdp::PortalSession session{nullptr};
    session.setStreamingEnabled(true);

    signal(SIGINT, [](int) {
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

    auto result = application.exec();

    file.close();

    return result;
}
