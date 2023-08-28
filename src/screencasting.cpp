/*
    SPDX-FileCopyrightText: 2020 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "qwayland-zkde-screencast-unstable-v1.h"
#include "screencasting_p.h"
#include <QDebug>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QWaylandClientExtensionTemplate>
#include <qpa/qplatformnativeinterface.h>
#include <qtwaylandclientversion.h>

class ScreencastingStreamPrivate : public QtWayland::zkde_screencast_stream_unstable_v1
{
public:
    ScreencastingStreamPrivate(ScreencastingStream *q)
        : q(q)
    {
    }
    ~ScreencastingStreamPrivate()
    {
        close();
        q->deleteLater();
    }

    void zkde_screencast_stream_unstable_v1_created(uint32_t node) override
    {
        m_nodeId = node;
        Q_EMIT q->created(node);
    }

    void zkde_screencast_stream_unstable_v1_closed() override
    {
        Q_EMIT q->closed();
    }

    void zkde_screencast_stream_unstable_v1_failed(const QString &error) override
    {
        Q_EMIT q->failed(error);
    }

    uint m_nodeId = 0;
    QSize m_size;
    QPointer<ScreencastingStream> q;
};

ScreencastingStream::ScreencastingStream(QObject *parent)
    : QObject(parent)
    , d(new ScreencastingStreamPrivate(this))
{
}

ScreencastingStream::~ScreencastingStream() = default;

quint32 ScreencastingStream::nodeId() const
{
    return d->m_nodeId;
}

QSize ScreencastingStream::size() const
{
    return d->m_size;
}

class ScreencastingPrivate : public QWaylandClientExtensionTemplate<ScreencastingPrivate>, public QtWayland::zkde_screencast_unstable_v1
{
public:
    ScreencastingPrivate(Screencasting *q)
        : QWaylandClientExtensionTemplate<ScreencastingPrivate>(ZKDE_SCREENCAST_UNSTABLE_V1_STREAM_REGION_SINCE_VERSION)
        , q(q)
    {
        initialize();

        if (!isInitialized()) {
            qWarning() << "Remember requesting the interface on your desktop file: X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1";
        }
        Q_ASSERT(isInitialized());
    }

    ~ScreencastingPrivate()
    {
        if (isActive()) {
            destroy();
        }
    }

    Screencasting *const q;
};

Screencasting::Screencasting(QObject *parent)
    : QObject(parent)
    , d(new ScreencastingPrivate(this))
{
}

Screencasting::~Screencasting() = default;

ScreencastingStream *Screencasting::createOutputStream(QScreen *screen, Screencasting::CursorMode mode)
{
    if (!d->isActive()) {
        return nullptr;
    }

    wl_output *output = (wl_output *)QGuiApplication::platformNativeInterface()->nativeResourceForScreen("output", screen);

    if (!output) {
        return nullptr;
    }

    auto stream = new ScreencastingStream(this);
    stream->setObjectName(screen->name());
    stream->d->init(d->stream_output(output, mode));
    stream->d->m_size = screen->virtualSize();
    return stream;
}

ScreencastingStream *Screencasting::createWorkspaceStream(Screencasting::CursorMode mode)
{
    const auto outputs = qGuiApp->screens();
    if (outputs.isEmpty()) {
        return nullptr;
    }
    if (outputs.count() == 1) {
        return createOutputStream(outputs.first(), mode);
    }

    QRegion r;

    QRect workspace;
    const auto screens = qGuiApp->screens();
    for (QScreen *screen : screens) {
        workspace |= screen->geometry();
    }
    return createRegionStream(workspace, 1, mode);
}

ScreencastingStream *Screencasting::createRegionStream(const QRect &g, qreal scale, Screencasting::CursorMode mode)
{
    auto stream = new ScreencastingStream(this);
    stream->d->init(d->stream_region(g.x(), g.y(), g.width(), g.height(), wl_fixed_from_double(scale), mode));
    stream->d->m_size = g.size();
    return stream;
}

void Screencasting::destroy()
{
    d.reset(nullptr);
}
