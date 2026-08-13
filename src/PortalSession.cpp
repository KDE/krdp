// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "PortalSession.h"

#include <QGuiApplication>
#include <QQueue>

#include <unistd.h>

#include <KConfigGroup>
#include <KSharedConfig>

#include "EiConnection.h"
#include "PortalSession_p.h"
#include "krdp_logging.h"
#include "xdp_dbus_remotedesktop_interface.h"
#include "xdp_dbus_screencast_interface.h"

using namespace Qt::StringLiterals;

namespace KRdp
{

static const QString dbusService = QStringLiteral("org.freedesktop.portal.Desktop");
static const QString dbusPath = QStringLiteral("/org/freedesktop/portal/desktop");
static const QString dbusRequestInterface = QStringLiteral("org.freedesktop.portal.Request");
static const QString dbusResponse = QStringLiteral("Response");
static const QString dbusSessionInterface = QStringLiteral("org.freedesktop.portal.Session");

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalSessionStream &stream)
{
    arg.beginStructure();
    arg >> stream.nodeId;

    arg.beginMap();
    while (!arg.atEnd()) {
        QString key;
        QVariant map;
        arg.beginMapEntry();
        arg >> key >> map;
        arg.endMapEntry();
        stream.map.insert(key, map);
    }
    arg.endMap();
    arg.endStructure();

    return arg;
}

void PortalRequest::onStarted(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (!reply.isError()) {
        QDBusConnection::sessionBus().connect(QString{}, reply.value().path(), dbusRequestInterface, dbusResponse, this, SLOT(onFinished(uint, QVariantMap)));
    } else {
        m_callback(-1, {{QStringLiteral("errorMessage"), reply.error().message()}});
    }
    watcher->deleteLater();
}

void PortalRequest::onFinished(uint code, const QVariantMap &result)
{
    if (m_context) {
        m_callback(code, result);
    }
    deleteLater();
}

class KRDP_NO_EXPORT PortalSession::Private
{
public:
    Server *server = nullptr;

    std::unique_ptr<OrgFreedesktopPortalRemoteDesktopInterface> remoteInterface;
    std::unique_ptr<OrgFreedesktopPortalScreenCastInterface> screencastInterface;

    QDBusObjectPath sessionPath;
    QString mappingId;

    std::unique_ptr<EiConnection> eiConnection;
};

QString createHandleToken()
{
    return QStringLiteral("krdp%1").arg(QRandomGenerator::global()->generate());
}

PortalSession::PortalSession()
    : AbstractSession()
    , d(std::make_unique<Private>())
{
    d->remoteInterface = std::make_unique<OrgFreedesktopPortalRemoteDesktopInterface>(dbusService, dbusPath, QDBusConnection::sessionBus());
    d->screencastInterface = std::make_unique<OrgFreedesktopPortalScreenCastInterface>(dbusService, dbusPath, QDBusConnection::sessionBus());

    if (!d->remoteInterface->isValid() || !d->screencastInterface->isValid()) {
        qCWarning(KRDP) << "Could not connect to Freedesktop Remote Desktop Portal";
        return;
    }
}

PortalSession::~PortalSession()
{
    if (d->sessionPath.path().isEmpty()) {
        qCDebug(KRDP) << "No portal session to close (session was never created)";
        return;
    }

    auto closeMessage = QDBusMessage::createMethodCall(dbusService, d->sessionPath.path(), dbusSessionInterface, QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(closeMessage);

    qCDebug(KRDP) << "Closing Freedesktop Portal Session";
}

void PortalSession::start()
{
    qCDebug(KRDP) << "Initializing Freedesktop Portal Session";

    auto parameters = QVariantMap{
        {QStringLiteral("handle_token"), createHandleToken()},
        {QStringLiteral("session_handle_token"), createHandleToken()},
    };
    new PortalRequest(d->remoteInterface->CreateSession(parameters), this, &PortalSession::onCreateSession);
}

void PortalSession::sendEvent(const std::shared_ptr<QEvent> &event)
{
    if (isStarted() && d->eiConnection) {
        d->eiConnection->sendEvent(event, size(), d->mappingId);
    }
}

void PortalSession::onCreateSession(uint code, const QVariantMap &result)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not open a new remote desktop session, error code" << code;
        Q_EMIT error();
        return;
    }

    d->sessionPath = QDBusObjectPath(result.value(QStringLiteral("session_handle")).toString());

    static const uint PermissionsPersistUntilExplicitlyRevoked = 2;

    auto parameters = QVariantMap{
        {QStringLiteral("types"), 7u},
        {QStringLiteral("handle_token"), createHandleToken()},
        {QStringLiteral("persist_mode"), PermissionsPersistUntilExplicitlyRevoked},
    };
    // name is set explicitly as this is also used by the KCM
    KConfigGroup restorationGroup = KSharedConfig::openStateConfig(QStringLiteral("krdp-serverstaterc"))->group(QStringLiteral("General"));
    QString restoreToken = restorationGroup.readEntry(QStringLiteral("restorationToken"));

    // this is a compatibility path for krdp < 6.3 that used a different name and in .config
    // in 6.4 onwards it can be killed
    if (restoreToken.isEmpty()) {
        KConfigGroup restorationGroup = KSharedConfig::openConfig(QStringLiteral("krdp-serverrc"))->group(QStringLiteral("General"));
        restoreToken = restorationGroup.readEntry(QStringLiteral("restorationToken"));
    } // end compat

    if (!restoreToken.isEmpty()) {
        parameters[QStringLiteral("restore_token")] = restoreToken;
    }

    new PortalRequest(d->remoteInterface->SelectDevices(d->sessionPath, parameters), this, &PortalSession::onDevicesSelected);
}

void PortalSession::onDevicesSelected(uint code, const QVariantMap & /*result*/)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not select devices for remote desktop session, error code" << code;
        Q_EMIT error();
        return;
    }

    QVariantMap parameters;
    if (virtualMonitor()) {
        parameters = {{QStringLiteral("types"), 4u}}; // VIRTUAL
    } else {
        parameters = {{QStringLiteral("types"), 1u}, // MONITOR
                      {QStringLiteral("multiple"), activeStream().has_value()}};
    }
    parameters[QStringLiteral("cursor_mode")] = 4u; // Metadata

    new PortalRequest(d->screencastInterface->SelectSources(d->sessionPath, parameters), this, &PortalSession::onSourcesSelected);
}

void PortalSession::onSourcesSelected(uint code, const QVariantMap & /*result*/)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not select sources for screencast session, error code" << code;
        Q_EMIT error();
        return;
    }

    auto parameters = QVariantMap{
        {QStringLiteral("handle_token"), createHandleToken()},
    };
    new PortalRequest(d->remoteInterface->Start(d->sessionPath, QString{}, parameters), this, &PortalSession::onSessionStarted);
}

void KRdp::PortalSession::onSessionStarted(uint code, const QVariantMap &result)
{
    if (code != 0) {
        qCWarning(KRDP) << "Could not start screencast session, error code" << code;
        Q_EMIT error();
        return;
    }

    if (result.value(QStringLiteral("devices")).toUInt() == 0) {
        qCWarning(KRDP) << "No devices were granted" << result;
        Q_EMIT error();
        return;
    }

    KConfigGroup restorationGroup = KSharedConfig::openStateConfig(QStringLiteral("krdp-serverstaterc"))->group(QStringLiteral("General"));
    restorationGroup.writeEntry("restorationToken", result.value(QStringLiteral("restore_token")));

    const auto streams = qdbus_cast<QList<PortalSessionStream>>(result.value(QStringLiteral("streams")));
    if (streams.isEmpty()) {
        qCWarning(KRDP) << "No screencast streams supplied";
        Q_EMIT error();
        return;
    }

    auto watcher = new QDBusPendingCallWatcher(d->screencastInterface->OpenPipeWireRemote(d->sessionPath, QVariantMap{}));
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, streams](QDBusPendingCallWatcher *watcher) {
        auto reply = QDBusReply<QDBusUnixFileDescriptor>(*watcher);
        if (reply.isValid()) {
            auto streamIndex = activeStream().value_or(0);
            if (streamIndex < 0 || streamIndex >= streams.size()) {
                qCWarning(KRDP) << "Requested monitor index out of range, using first monitor";
                setActiveStream(0);
                streamIndex = 0;
            }
            auto fd = reply.value();
            const int pipeWireFd = fd.takeFileDescriptor();
            QRect logicalBounds;
            bool firstScreen = true;
            for (int i = activeStream() ? streamIndex : 0; i < (activeStream() ? streamIndex + 1 : streams.size()); ++i) {
                const auto &portalStream = streams.at(i);
                auto screen = std::make_unique<Stream>();
                screen->nodeId = portalStream.nodeId;
                screen->size = qdbus_cast<QSize>(portalStream.map.value(u"size"_s));
                screen->position = qdbus_cast<QPoint>(portalStream.map.value(u"position"_s));
                screen->pipeWireFd = firstScreen ? pipeWireFd : dup(pipeWireFd);
                if (screen->pipeWireFd < 0) {
                    qCWarning(KRDP) << "Could not duplicate PipeWire remote fd";
                    Q_EMIT error();
                    return;
                }

                if (d->mappingId.isEmpty()) {
                    d->mappingId = portalStream.map.value(u"mapping_id"_s).toString();
                }
                logicalBounds = logicalBounds.united(QRect(screen->position, screen->size));
                addScreen(std::move(screen));
                firstScreen = false;
            }
            setLogicalSize(logicalBounds.size());
            QDBusConnection::sessionBus().connect(u"org.freedesktop.portal.Desktop"_s,
                                                  d->sessionPath.path(),
                                                  u"org.freedesktop.portal.Session"_s,
                                                  u"Closed"_s,
                                                  this,
                                                  SLOT(onSessionClosed()));
            qCDebug(KRDP) << "Started Freedesktop Portal session";
            setStarted(true);
        } else {
            qCWarning(KRDP) << "Could not open pipewire remote";
            Q_EMIT error();
        }
        watcher->deleteLater();
    });

    connectToEis();
}

void PortalSession::onSessionClosed()
{
    qCWarning(KRDP) << "Portal session was closed!";
    Q_EMIT error();
}

void PortalSession::connectToEis()
{
    auto watcher = new QDBusPendingCallWatcher(d->remoteInterface->ConnectToEIS(d->sessionPath, QVariantMap{}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        if (reply.isError()) {
            qCWarning(KRDP) << "Could not connect portal session to EIS:" << reply.error().message();
            Q_EMIT error();
            return;
        }

        d->eiConnection = std::make_unique<EiConnection>(reply.value().takeFileDescriptor(), this);
        if (!d->eiConnection->isValid()) {
            Q_EMIT error();
            return;
        }
        connect(d->eiConnection.get(), &EiConnection::error, this, &PortalSession::error);
    });
}
}

#include "moc_PortalSession_p.cpp"

#include "moc_PortalSession.cpp"
