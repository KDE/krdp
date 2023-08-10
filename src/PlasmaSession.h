// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

#include <QDBusPendingCallWatcher>
#include <QPoint>
#include <QPointer>

#include "AbstractSession.h"
#include "krdp_export.h"

namespace KRdp
{

struct VideoFrame;
class Server;

/**
 * An implementation of the Plasma screencasting wayland protocol.
 */
class KRDP_EXPORT PlasmaSession : public AbstractSession
{
    Q_OBJECT

public:
    PlasmaSession(Server *server);
    ~PlasmaSession() override;

    void sendEvent(QEvent *event) override;

private:
    void onPacketReceived(const PipeWireEncodedStream::Packet &data);

    class Private;
    const std::unique_ptr<Private> d;
};

}
