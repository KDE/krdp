// SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleix.pol_gonzalez@mercedes-benz.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "AbstractSession.h"
#include <PipeWireEncodedStream>
#include <PipeWireSourceStream>
#include <QSet>

namespace KRdp
{

class KRDP_NO_EXPORT AbstractSession::Private
{
public:
    std::unique_ptr<PipeWireEncodedStream> encodedStream;

    std::optional<int> activeStream;
    std::optional<VirtualMonitor> virtualMonitor;
    bool started = false;
    bool enabled = false;
    QSize size;
    QSize logicalSize;
    std::optional<quint32> frameRate = 60;
    std::optional<quint8> quality;
    QSet<QObject *> enableRequests;
};

AbstractSession::AbstractSession()
    : QObject()
    , d(std::make_unique<Private>())
{
}

AbstractSession::~AbstractSession()
{
    if (d->encodedStream) {
        d->encodedStream->stop();
    }
}

QSize AbstractSession::logicalSize() const
{
    return d->logicalSize;
}

int AbstractSession::activeStream() const
{
    return d->activeStream.value_or(-1);
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
    if (d->encodedStream) {
        d->encodedStream->setRequestedSize(d->virtualMonitor->size);
    }
}

void AbstractSession::setVideoQuality(quint8 quality)
{
    d->quality = quality;
    if (d->encodedStream) {
        d->encodedStream->setQuality(quality);
    }
}

bool AbstractSession::streamingEnabled() const
{
    if (d->encodedStream) {
        return d->encodedStream->isActive();
    }
    return false;
}

void AbstractSession::setStreamingEnabled(bool enable)
{
    d->enabled = enable;
    if (d->encodedStream) {
        if (enable && d->started) {
            d->encodedStream->start();
        } else {
            d->encodedStream->stop();
        }
    }
}

void AbstractSession::setVideoFrameRate(quint32 framerate)
{
    d->frameRate = framerate;
    if (d->encodedStream) {
        d->encodedStream->setMaxFramerate({framerate, 1});
        // this buffers 1 second of frames and drops after that
        d->encodedStream->setMaxPendingFrames(framerate);
    }
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

PipeWireEncodedStream *AbstractSession::stream()
{
    if (!d->encodedStream) {
        d->encodedStream = std::make_unique<PipeWireEncodedStream>();
        if (d->frameRate) {
            d->encodedStream->setMaxFramerate({d->frameRate.value(), 1});
        }
        if (d->virtualMonitor) {
            d->encodedStream->setRequestedSize(d->virtualMonitor->size);
        }
        if (d->quality) {
            d->encodedStream->setQuality(d->quality.value());
        }
    }
    return d->encodedStream.get();
}

void AbstractSession::setStarted(bool s)
{
    d->started = s;
    if (s) {
        if (d->enabled) {
            d->encodedStream->start();
        }
        Q_EMIT started();
    }
}

void AbstractSession::requestStreamingEnable(QObject *requester)
{
    d->enableRequests.insert(requester);
    connect(requester, &QObject::destroyed, this, &AbstractSession::requestStreamingDisable);
    setStreamingEnabled(true);
}

void AbstractSession::requestStreamingDisable(QObject *requester)
{
    if (!d->enableRequests.contains(requester)) {
        return;
    }
    disconnect(requester, &QObject::destroyed, this, &AbstractSession::requestStreamingDisable);
    d->enableRequests.remove(requester);
    if (d->enableRequests.size() == 0) {
        setStreamingEnabled(false);
    }
}

}

#include "AbstractSession.moc"
