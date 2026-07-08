/*
    SPDX-FileCopyrightText: 2020 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#pragma once

#include <QScreen>
#include <memory>

#include "krdp_export.h"

class ScreencastingPrivate;
class ScreencastingStreamPrivate;
class ScreencastingStream : public QObject
{
    Q_OBJECT
public:
    ScreencastingStream(QObject *parent);
    ~ScreencastingStream() override;

    quint32 nodeId() const;
    QSize size() const;

    quint64 objectSerial() const;

Q_SIGNALS:
    void created(quint32 nodeid);
    void failed(const QString &error);
    void closed();
    void serial(quint64 serial);

private:
    friend class Screencasting;
    std::unique_ptr<ScreencastingStreamPrivate> d;
};

class Screencasting : public QObject
{
    Q_OBJECT
public:
    explicit Screencasting(QObject *parent = nullptr);
    ~Screencasting() override;

    enum CursorMode {
        Hidden = 1,
        Embedded = 2,
        Metadata = 4,
    };
    Q_ENUM(CursorMode)

    ScreencastingStream *createOutputStream(QScreen *screen, CursorMode mode);
    ScreencastingStream *createWorkspaceStream(CursorMode mode);
    ScreencastingStream *createRegionStream(QRect g, qreal scale, CursorMode mode);
    ScreencastingStream *createVirtualMonitorStream(const QString &name, const QSize &resolution, qreal dpr, CursorMode mode);

    void destroy();

Q_SIGNALS:
    void initialized();
    void removed();
    void sourcesChanged();

private:
    std::unique_ptr<ScreencastingPrivate> d;
};
