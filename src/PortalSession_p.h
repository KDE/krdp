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

#include "krdp_export.h"

namespace KRdp
{

/**
 * Convenience class to help with making requests to the Desktop portal less cumbersome to work with.
 *
 * A request to the portal requires first calling a method on the portal, which
 * will return an object path that should be listened to for a signal. The
 * signal provides the actual proper data for the original method call.
 */
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
        // We need to wait here because otherwise we risk the signal arriving
        // before we know what path to listen on.
        watcher->waitForFinished();
        onStarted(watcher);
    }

private:
    void onStarted(QDBusPendingCallWatcher *watcher);
    Q_SLOT void onFinished(uint code, const QVariantMap &result);

    QPointer<QObject> m_context;
    std::function<void(uint, const QVariantMap &)> m_callback;
};

struct PortalSessionStream {
    uint nodeId;
    QVariantMap map;
};

}

Q_DECLARE_METATYPE(KRdp::PortalSessionStream)
