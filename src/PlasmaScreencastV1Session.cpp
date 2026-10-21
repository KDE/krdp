// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PlasmaScreencastV1Session.h"

#include "EiConnection.h"
#include "krdp_logging.h"
#include "screencasting_p.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QGuiApplication>

using namespace Qt::StringLiterals;

namespace KRdp
{

class KRDP_NO_EXPORT PlasmaScreencastV1Session::Private
{
public:
    Server *server = nullptr;

    Screencasting m_screencasting;
    std::unique_ptr<ScreencastingStream> request;
    std::unique_ptr<EiConnection> eiConnection;
    int eisCookie = 0;
};

PlasmaScreencastV1Session::PlasmaScreencastV1Session()
    : AbstractSession()
    , d(std::make_unique<Private>())
{
    constexpr auto kwinService = "org.kde.KWin"_L1;
    constexpr auto kwinRemoteDesktopPath = "/org/kde/KWin/EIS/RemoteDesktop"_L1;
    constexpr auto kwinRemoteDesktopInterface = "org.kde.KWin.EIS.RemoteDesktop"_L1;
    auto msg = QDBusMessage::createMethodCall(kwinService, kwinRemoteDesktopPath, kwinRemoteDesktopInterface, "connectToEIS"_L1);
    msg.setArguments({7}); // Keyboard | Pointer | TouchScreen
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        QDBusPendingReply<QDBusUnixFileDescriptor, int> reply = *watcher;
        if (reply.isError()) {
            qCWarning(KRDP) << "Failed to connect to EIS:" << reply.error();
            return;
        }
        d->eisCookie = reply.argumentAt<1>();
        d->eiConnection = std::make_unique<EiConnection>(reply.argumentAt<0>().takeFileDescriptor(), this);
        if (!d->eiConnection->isValid()) {
            qCWarning(KRDP) << "Failed to create EIS connection";
        }
    });
}

PlasmaScreencastV1Session::~PlasmaScreencastV1Session()
{
    if (d->eisCookie) {
        constexpr auto kwinService = "org.kde.KWin"_L1;
        constexpr auto kwinRemoteDesktopPath = "/org/kde/KWin/EIS/RemoteDesktop"_L1;
        constexpr auto kwinRemoteDesktopInterface = "org.kde.KWin.EIS.RemoteDesktop"_L1;
        auto msg = QDBusMessage::createMethodCall(kwinService, kwinRemoteDesktopPath, kwinRemoteDesktopInterface, QStringLiteral("disconnect"));
        msg.setArguments({d->eisCookie});
        QDBusConnection::sessionBus().send(msg);
    }
    qCDebug(KRDP) << "Closing Plasma Remote Session";
}

void PlasmaScreencastV1Session::start()
{
    if (auto vm = virtualMonitor()) {
        d->request.reset(d->m_screencasting.createVirtualMonitorStream(vm->name, vm->size, vm->dpr, Screencasting::Metadata));
    } else if (const auto streamIndex = activeStream()) {
        if (*streamIndex >= qApp->screens().size()) {
            qCWarning(KRDP) << "Invalid monitor index" << *streamIndex;
            return;
        }
        auto screen = qApp->screens().at(*streamIndex);
        d->request.reset(d->m_screencasting.createOutputStream(screen, Screencasting::Metadata));
    } else {
        d->request.reset(d->m_screencasting.createWorkspaceStream(Screencasting::Metadata));
    }
    connect(d->request.get(), &ScreencastingStream::failed, this, &PlasmaScreencastV1Session::error);
    connect(d->request.get(), &ScreencastingStream::created, this, [this](uint nodeId) {
        qCDebug(KRDP) << "Started Plasma session";

        setLogicalSize(d->request->size());
        setNodeId(nodeId);
        setStarted(true);
    });

    connect(d->request.get(), &ScreencastingStream::serial, this, [this](quint64 serial) {
        setObjectSerial(serial);
    });
}

void PlasmaScreencastV1Session::sendEvent(const std::shared_ptr<QEvent> &event)
{
    if (isStarted() && d->eiConnection) {
        d->eiConnection->sendEvent(event, size(), {});
    }
}
}
