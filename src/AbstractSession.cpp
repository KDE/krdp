// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "AbstractSession.h"
#include <unistd.h>

namespace KRdp
{

class KRDP_NO_EXPORT AbstractSession::Private
{
public:
    std::optional<int> activeStream;
    std::optional<VirtualMonitor> virtualMonitor;
    bool started = false;
    QSize size;
    QSize logicalSize;
    quint32 nodeId = 0;
    int pipeWireFd = -1;
};

AbstractSession::AbstractSession()
    : QObject()
    , d(std::make_unique<Private>())
{
}

AbstractSession::~AbstractSession()
{
    if (d->pipeWireFd >= 0) {
        close(d->pipeWireFd);
    }
}

QSize AbstractSession::logicalSize() const
{
    return d->logicalSize;
}

std::optional<int> AbstractSession::activeStream() const
{
    return d->activeStream;
}

std::optional<VirtualMonitor> AbstractSession::virtualMonitor() const
{
    return d->virtualMonitor;
}

void AbstractSession::setActiveStream(int stream)
{
    Q_ASSERT(!d->virtualMonitor);
    d->activeStream = stream;
}

void AbstractSession::setVirtualMonitor(const VirtualMonitor &virtualMonitor)
{
    Q_ASSERT(!d->activeStream.has_value());
    d->virtualMonitor = virtualMonitor;
}

bool AbstractSession::isStarted() const
{
    return d->started;
}

void AbstractSession::setSize(QSize size)
{
    d->size = size;
}

void AbstractSession::setLogicalSize(QSize size)
{
    d->logicalSize = size;
}

QSize AbstractSession::size() const
{
    return d->size;
}

quint32 AbstractSession::nodeId() const
{
    return d->nodeId;
}

int AbstractSession::takePipeWireFd()
{
    return std::exchange(d->pipeWireFd, -1);
}

void AbstractSession::setNodeId(quint32 nodeId)
{
    d->nodeId = nodeId;
}

void AbstractSession::setPipeWireFd(int fd)
{
    if (d->pipeWireFd >= 0) {
        close(d->pipeWireFd);
    }
    d->pipeWireFd = fd;
}

void AbstractSession::setStarted(bool s)
{
    d->started = s;
    if (s) {
        Q_EMIT started();
    }
}

}

#include "AbstractSession.moc"
