// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// This file is roughly based on grd-rdp-network-autodetection.c from Gnome
// Remote Desktop which is:
//
// SPDX-FileCopyrightText: 2021 Pascal Nowack
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "NetworkDetection.h"

#include <ranges>

#include <atomic>

#include <QQueue>
#include <QTimer>

#include "PeerContext_p.h"
#include "RdpConnection.h"

#include "krdp_logging.h"

namespace KRdp
{

namespace clk = std::chrono;

constexpr auto rttUpdateInterval = clk::milliseconds(70);
constexpr auto rttAverageInterval = clk::milliseconds(500);
constexpr auto networkResultInterval = clk::seconds(1);
constexpr auto rttRequestMaxAge = clk::seconds(30);

constexpr auto bandwidthMeasureDuration = clk::milliseconds(500);
constexpr auto bandwidthMeasureInterval = clk::seconds(2);
constexpr double bandwidthSmoothingWeight = 0.5;

BOOL rttMeasureResponse(rdpAutoDetect *rdpAutodetect, RDP_TRANSPORT_TYPE, uint16_t sequence)
{
    auto context = reinterpret_cast<PeerContext *>(rdpAutodetect->context);
    if (context->networkDetection->onRttMeasureResponse(sequence)) {
        return TRUE;
    }
    return FALSE;
}

BOOL bwMeasureResults(rdpAutoDetect *rdpAutodetect, RDP_TRANSPORT_TYPE, uint16_t, uint16_t, uint32_t timeDelta, uint32_t byteCount)
{
    auto context = reinterpret_cast<PeerContext *>(rdpAutodetect->context);
    if (context->networkDetection->onBandwidthMeasureResults(timeDelta, byteCount)) {
        return TRUE;
    }
    return FALSE;
}

struct RTTMeasurement {
    clk::system_clock::time_point measurementTime;
    clk::system_clock::duration roundTripTime;
};

class NetworkDetection::Private
{
public:
    uint16_t nextSequenceNumber();

    RdpConnection *session = nullptr;
    rdpAutoDetect *rdpAutodetect = nullptr;

    State state = State::None;

    uint16_t sequenceNumber = 0;

    double smoothedBandwidthBps = 0.0;
    bool hasSmoothedBandwidth = false;

    bool rttEnabled = false;
    clk::system_clock::time_point lastRttUpdate;
    QHash<uint16_t, clk::system_clock::time_point> rttRequests;
    std::vector<RTTMeasurement> rttMeasurements;

    std::atomic<clk::system_clock::rep> minimumRttTicks{0};
    std::atomic<clk::system_clock::rep> averageRttTicks{0};
    std::atomic<uint32_t> averageBandwidthBps{0};

    clk::system_clock::time_point lastNetworkResult;

    clk::system_clock::time_point bandwidthMeasureStartTime;
    clk::system_clock::time_point lastBandwidthMeasureStart;
};

NetworkDetection::NetworkDetection(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
}

NetworkDetection::~NetworkDetection() = default;

std::chrono::system_clock::duration NetworkDetection::minimumRTT() const
{
    return clk::system_clock::duration(d->minimumRttTicks.load());
}

std::chrono::system_clock::duration NetworkDetection::averageRTT() const
{
    return clk::system_clock::duration(d->averageRttTicks.load());
}

quint32 NetworkDetection::bandwidth() const
{
    return d->averageBandwidthBps.load() * 8 / 1000;
}

void NetworkDetection::initialize()
{
    d->rdpAutodetect = d->session->rdpPeerContext()->autodetect;
    d->rdpAutodetect->RTTMeasureResponse = rttMeasureResponse;
    d->rdpAutodetect->BandwidthMeasureResults = bwMeasureResults;
}

void NetworkDetection::startBandwidthMeasure()
{
    if (d->state != State::None) {
        return;
    }

    d->state = State::PendingStop;
    d->bandwidthMeasureStartTime = clk::system_clock::now();
    d->rdpAutodetect->BandwidthMeasureStart(d->rdpAutodetect, RDP_TRANSPORT_TCP, 0);
}

void NetworkDetection::stopBandwidthMeasure()
{
    if (d->state != State::PendingStop) {
        return;
    }

    d->state = State::PendingResults;
    d->rdpAutodetect->BandwidthMeasureStop(d->rdpAutodetect, RDP_TRANSPORT_TCP, 0, 0);
}

void NetworkDetection::update()
{
    if (d->session->state() != RdpConnection::State::Streaming) {
        return;
    }

    auto now = clk::system_clock::now();

    if (d->state == State::PendingStop && (now - d->bandwidthMeasureStartTime) >= bandwidthMeasureDuration) {
        stopBandwidthMeasure();
    } else if (d->state == State::None && (now - d->lastBandwidthMeasureStart) >= bandwidthMeasureInterval) {
        d->lastBandwidthMeasureStart = now;
        startBandwidthMeasure();
    }

    if ((now - d->lastRttUpdate) < rttUpdateInterval) {
        return;
    }

    d->lastRttUpdate = now;

    d->rttRequests.removeIf([now](const auto &it) {
        return (now - it.value()) > rttRequestMaxAge;
    });

    auto sequence = d->nextSequenceNumber();
    d->rttRequests.insert(sequence, now);
    d->rdpAutodetect->RTTMeasureRequest(d->rdpAutodetect, RDP_TRANSPORT_TCP, sequence);
}

bool NetworkDetection::onRttMeasureResponse(uint16_t sequence)
{
    if (!d->rttRequests.contains(sequence)) {
        return true;
    }

    RTTMeasurement rtt;
    rtt.measurementTime = clk::system_clock::now();
    rtt.roundTripTime = rtt.measurementTime - d->rttRequests.take(sequence);

    if (rtt.roundTripTime.count() <= 0) {
        return true;
    }

    d->rttMeasurements.push_back(std::move(rtt));

    updateAverageRtt();

    return true;
}

bool NetworkDetection::onBandwidthMeasureResults(uint32_t timeDelta, uint32_t byteCount)
{
    if (d->state != State::PendingResults) {
        return true;
    }

    d->state = State::None;

    if (timeDelta == 0 || byteCount == 0) {
        return true;
    }

    const auto bytesPerSecond = static_cast<uint32_t>((static_cast<uint64_t>(byteCount) * 1000ULL) / static_cast<uint64_t>(timeDelta));
    if (!d->hasSmoothedBandwidth) {
        d->hasSmoothedBandwidth = true;
        d->smoothedBandwidthBps = bytesPerSecond;
    } else {
        d->smoothedBandwidthBps = (1.0 - bandwidthSmoothingWeight) * d->smoothedBandwidthBps + bandwidthSmoothingWeight * bytesPerSecond;
    }
    d->averageBandwidthBps.store(static_cast<uint32_t>(d->smoothedBandwidthBps));
    Q_EMIT bandwidthChanged();

    updateAverageRtt();

    return true;
}

void NetworkDetection::updateAverageRtt()
{
    auto now = clk::system_clock::now();
    d->rttMeasurements.erase(std::remove_if(d->rttMeasurements.begin(),
                                            d->rttMeasurements.end(),
                                            [now](const auto &measurement) {
                                                return (now - measurement.measurementTime) > rttAverageInterval;
                                            }),
                             d->rttMeasurements.end());
    if (d->rttMeasurements.empty()) {
        return;
    }

    auto minimum = std::numeric_limits<clk::system_clock::duration>::max();
    auto sum = clk::system_clock::duration(0);
    std::for_each(d->rttMeasurements.begin(), d->rttMeasurements.end(), [&minimum, &sum](const auto &measurement) {
        minimum = std::min(minimum, measurement.roundTripTime);
        sum = sum + measurement.roundTripTime;
    });
    const auto average = sum / d->rttMeasurements.size();
    d->minimumRttTicks.store(minimum.count());
    d->averageRttTicks.store(average.count());

    Q_EMIT rttChanged();

    const auto bandwidthBps = d->averageBandwidthBps.load();
    if (bandwidthBps == 0) {
        return;
    }

    if ((now - d->lastNetworkResult) < networkResultInterval) {
        return;
    }

    d->lastNetworkResult = now;

    rdpNetworkCharacteristicsResult result;
    result.type = RDP_NETCHAR_RESULT_TYPE_BASE_RTT_BW_AVG_RTT;
    result.baseRTT = clk::duration_cast<clk::milliseconds>(minimum).count();
    result.averageRTT = clk::duration_cast<clk::milliseconds>(average).count();
    result.bandwidth = bandwidthBps;
    d->rdpAutodetect->NetworkCharacteristicsResult(d->rdpAutodetect, RDP_TRANSPORT_TCP, d->nextSequenceNumber(), &result);
}

uint16_t NetworkDetection::Private::nextSequenceNumber()
{
    auto sequence = sequenceNumber;
    while (sequence == 0 || rttRequests.contains(sequence)) {
        ++sequence;
    }
    sequenceNumber = static_cast<uint16_t>(sequence + 1);
    return sequence;
}

} // namespace KRdp

#include "moc_NetworkDetection.cpp"
