// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

#include <QDBusPendingCallWatcher>
#include <QObject>
#include <QPoint>
#include <QPointer>

#include "AbstractSession.h"
#include "krdp_export.h"

namespace KRdp
{

struct VideoFrame;
class Server;

/**
 * A FreeDesktop Remote Desktop Portal session.
 *
 * This encapsulates all the required setup to start a FreeDesktop Remote
 * Desktop Portal session including input sending and video streaming.
 */
class KRDP_EXPORT PortalSession : public AbstractSession
{
    Q_OBJECT

public:
    explicit PortalSession();
    ~PortalSession() override;

    void start() override;
    /**
     * Send a new event to the portal.
     *
     * \param event The new event to send.
     */
    void sendEvent(const std::shared_ptr<QEvent> &event) override;

    void setClipboardData(std::unique_ptr<QMimeData> data) override;

private:
    void onCreateSession(uint code, const QVariantMap &result);
    void onDevicesSelected(uint code, const QVariantMap &result);
    void onSourcesSelected(uint code, const QVariantMap &result);
    void onSessionStarted(uint code, const QVariantMap &result);
    void onPacketReceived(const PipeWireEncodedStream::Packet &data);
    Q_SLOT void onSessionClosed();

    class Private;
    const std::unique_ptr<Private> d;
};

}
