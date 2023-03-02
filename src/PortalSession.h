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
#include <QPointer>

#include <KPipeWire/PipeWireSourceStream>

#include "krdp_export.h"

namespace KRdp
{

struct VideoFrame;

class Server;

class PortalRequest : public QObject
{
    Q_OBJECT
public:
    template<typename ContextType, typename Callback>
    PortalRequest(const QDBusPendingCall &call, ContextType *context, Callback callback)
    {
        m_context = context;

        if constexpr (std::is_member_function_pointer<Callback>::value) {
            m_callback = std::bind(callback, context, std::placeholders::_1, std::placeholders::_2);
        } else {
            m_callback = callback;
        }

        auto watcher = new QDBusPendingCallWatcher(call);
        watcher->waitForFinished();
        onStarted(watcher);
    }

private:
    void onStarted(QDBusPendingCallWatcher *watcher);
    Q_SLOT void onFinished(uint code, const QVariantMap &result);

    QPointer<QObject> m_context;
    std::function<void(uint, const QVariantMap &)> m_callback;
};

class KRDP_EXPORT PortalSession : public QObject
{
    Q_OBJECT

public:
    struct Stream {
        uint nodeId;
        QVariantMap map;
    };

    PortalSession(Server *server);
    ~PortalSession();

    Q_SIGNAL void started();
    Q_SIGNAL void error();

    void sendEvent(QEvent *event);

    Q_SIGNAL void frameReceived(const VideoFrame &frame);

private:
    void onCreateSession(uint code, const QVariantMap &result);
    void onDevicesSelected(uint code, const QVariantMap & /*result*/);
    void onSourcesSelected(uint code, const QVariantMap & /*result*/);
    void onSessionStarted(uint code, const QVariantMap &result);
    void onPacketReceived(const QByteArray &data);

    class Private;
    const std::unique_ptr<Private> d;
};

}

Q_DECLARE_METATYPE(KRdp::PortalSession::Stream)
