// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include "krdp_export.h"

#include <memory>
#include <optional>

#include <QEvent>
#include <QObject>
#include <QSize>
#include <QString>

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

class KRDP_EXPORT AbstractSession : public QObject
{
    Q_OBJECT
public:
    AbstractSession();
    ~AbstractSession() override;

    /**
     * Properties have been initialised and we can start the session
     */
    virtual void start() = 0;

    void setActiveStream(int stream);
    void setVirtualMonitor(const VirtualMonitor &vm);
    quint32 nodeId() const;
    int takePipeWireFd();

    /**
     * Set the system's clipboard data.
     *
     * The data is provided by the remote RDP client.
     */
    virtual void setClipboardData(std::unique_ptr<QMimeData> data) = 0;

    /**
     * Send a new event to the portal.
     *
     * \param event The new event to send.
     */
    virtual void sendEvent(const std::shared_ptr<QEvent> &event) = 0;

Q_SIGNALS:
    void started();
    void error();

    /**
     * Emitted whenever the system's clipboard data changes.
     */
    void clipboardDataChanged(const QMimeData *data);

protected:
    bool isStarted() const;
    QSize size() const;
    QSize logicalSize() const;
    std::optional<VirtualMonitor> virtualMonitor() const;
    std::optional<int> activeStream() const;

    void setStarted(bool started);
    void setSize(QSize size);
    void setLogicalSize(QSize size);
    void setNodeId(quint32 nodeId);
    void setPipeWireFd(int fd);

private:
    class Private;
    const std::unique_ptr<Private> d;
};

}
