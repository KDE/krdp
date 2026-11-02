// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <optional>

#include <QDBusPendingCallWatcher>
#include <QEvent>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QString>

#include "krdp_export.h"

class QMimeData;

namespace KRdp
{

struct VideoFrame;
class Server;

struct VirtualMonitor {
    QString name;
    QSize size;
    qreal dpr;
};

/**
 * A FreeDesktop Remote Desktop Portal session.
 *
 * This encapsulates all the required setup to start a FreeDesktop Remote
 * Desktop Portal session including input sending and video streaming.
 */
class KRDP_EXPORT PortalSession : public QObject
{
    Q_OBJECT

public:
    explicit PortalSession();
    ~PortalSession() override;

    /**
     * Properties have been initialised and we can start the session
     */
    void start();

    void setActiveStream(int stream);
    void setVirtualMonitor(const VirtualMonitor &vm);
    quint32 nodeId() const;
    int takePipeWireFd();

    void setSize(QSize size);

    quint64 objectSerial() const;

    /**
     * Set the system's clipboard data.
     *
     * The data is provided by the remote RDP client.
     */
    void setClipboardData(std::unique_ptr<QMimeData> data);

    /**
     * Send a new event to the portal.
     *
     * \param event The new event to send.
     */
    void sendEvent(const std::shared_ptr<QEvent> &event);

Q_SIGNALS:
    void started();
    void error();

    /**
     * Emitted whenever the system's clipboard data changes.
     */
    void clipboardDataChanged(const QMimeData *data);

private:
    bool isStarted() const;
    QSize size() const;
    QSize logicalSize() const;
    std::optional<VirtualMonitor> virtualMonitor() const;
    std::optional<int> activeStream() const;

    void setStarted(bool started);
    void setLogicalSize(QSize size);
    void setNodeId(quint32 nodeId);
    void setPipeWireFd(int fd);
    void setObjectSerial(quint64 objectSerial);

    void connectToEis();

    void onCreateSession(uint code, const QVariantMap &result);
    void onDevicesSelected(uint code, const QVariantMap &result);
    void onSourcesSelected(uint code, const QVariantMap &result);
    void onSessionStarted(uint code, const QVariantMap &result);
    Q_SLOT void onSessionClosed();

    class Private;
    const std::unique_ptr<Private> d;
};

}
