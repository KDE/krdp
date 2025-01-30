// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <chrono>
#include <memory>

#include <QObject>

#include <freerdp/freerdp.h>
#include <freerdp/types.h>

namespace KRdp
{

class RdpConnection;

class NetworkDetection : public QObject
{
    Q_OBJECT

public:
    enum class State {
        None,
        PendingStop,
        QueuedStop,
        PendingResults,
    };

    explicit NetworkDetection(RdpConnection *session);
    ~NetworkDetection();

    Q_PROPERTY(std::chrono::system_clock::duration minimumRTT READ minimumRTT NOTIFY rttChanged)
    std::chrono::system_clock::duration minimumRTT() const;
    Q_PROPERTY(std::chrono::system_clock::duration averageRTT READ averageRTT NOTIFY rttChanged)
    std::chrono::system_clock::duration averageRTT() const;

    Q_SIGNAL void rttChanged();

    void initialize();

    void startBandwidthMeasure();
    void stopBandwidthMeasure();

    void update();

private:
    friend BOOL rttMeasureResponse(rdpAutoDetect *, RDP_TRANSPORT_TYPE, uint16_t);
    friend BOOL bwMeasureResults(rdpAutoDetect *, RDP_TRANSPORT_TYPE, uint16_t, uint16_t, uint32_t, uint32_t);

    bool onRttMeasureResponse(uint16_t sequence);
    bool onBandwidthMeasureResults();

    void updateAverageRtt();

    class Private;
    const std::unique_ptr<Private> d;
};

}
