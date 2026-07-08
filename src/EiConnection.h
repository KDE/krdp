// SPDX-FileCopyrightText: 2026 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <vector>

#include <QObject>

#include "krdp_export.h"

class QEvent;
class QSize;
class QString;
class QSocketNotifier;
struct ei;

namespace KRdp
{

class EiDevice;
class EisPointerDevice;

class KRDP_EXPORT EiConnection : public QObject
{
    Q_OBJECT

public:
    explicit EiConnection(int fd, QObject *parent = nullptr);
    ~EiConnection() override;

    [[nodiscard]] bool isValid() const;
    void sendEvent(const std::shared_ptr<QEvent> &event, const QSize &streamSize, const QString &mappingId);

Q_SIGNALS:
    void error();

private:
    void processEisEvents();
    Q_SLOT void onEisReadyRead();

    std::unique_ptr<QSocketNotifier> m_eisNotifier;
    struct ei *m_ei = nullptr;
    std::vector<std::unique_ptr<EisPointerDevice>> m_pointerDevices;
    std::unique_ptr<EiDevice> m_keyboardDevice;
    std::unique_ptr<EiDevice> m_textDevice;
};

}
