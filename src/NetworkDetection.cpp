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

BOOL rttMeasureResponse(rdpContext *rdpContext, uint16_t sequence)
{
    auto context = reinterpret_cast<PeerContext *>(rdpContext);
    if (context->networkDetection->onRttMeasureResponse(sequence)) {
        return TRUE;
    }
    return FALSE;
}

BOOL bwMeasureResults(rdpContext *rdpContext, uint16_t)
{
    auto context = reinterpret_cast<PeerContext *>(rdpContext);
    if (context->networkDetection->onBandwidthMeasureResults()) {
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
    uint32_t nextSequenceNumber();

    RdpConnection *session = nullptr;
    rdpAutoDetect *rdpAutodetect = nullptr;

    State state = State::None;

    uint32_t sequenceNumber = 0;

    uint32_t lastBandwithMeasurement;

    bool rttEnabled = false;
    clk::system_clock::time_point lastRttUpdate;
    QHash<uint32_t, clk::system_clock::time_point> rttRequests;
    std::vector<RTTMeasurement> rttMeasurements;

    clk::system_clock::duration minimumRtt;
    clk::system_clock::duration averageRtt;

    clk::system_clock::time_point lastNetworkResult;
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
    return d->minimumRtt;
}

std::chrono::system_clock::duration NetworkDetection::averageRTT() const
{
    return d->averageRtt;
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
    d->rdpAutodetect->BandwidthMeasureStart(d->rdpAutodetect->context, 0);
}

void NetworkDetection::stopBandwidthMeasure()
{
    if (d->state != State::PendingStop) {
        return;
    }

    d->state = State::PendingResults;
    d->rdpAutodetect->BandwidthMeasureStop(d->rdpAutodetect->context, 0);
}

void NetworkDetection::update()
{
    if (d->session->state() != RdpConnection::State::Streaming) {
        return;
    }

    auto now = clk::system_clock::now();
    if ((now - d->lastRttUpdate) < rttUpdateInterval) {
        return;
    }

    d->lastRttUpdate = now;

    auto sequence = d->nextSequenceNumber();
    d->rttRequests.insert(sequence, now);
    d->rdpAutodetect->RTTMeasureRequest(d->rdpAutodetect->context, sequence);
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

bool NetworkDetection::onBandwidthMeasureResults()
{
    if (d->state != State::PendingResults) {
        return true;
    }

    d->state = State::None;

    if (d->rdpAutodetect->netCharBandwidth <= 0) {
        return true;
    }

    d->lastBandwithMeasurement = d->rdpAutodetect->netCharBandwidth;

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

    d->minimumRtt = std::numeric_limits<clk::system_clock::duration>::max();
    auto sum = clk::system_clock::duration(0);
    std::for_each(d->rttMeasurements.begin(), d->rttMeasurements.end(), [this, &sum](const auto &measurement) {
        d->minimumRtt = std::min(d->minimumRtt, measurement.roundTripTime);
        sum = sum + measurement.roundTripTime;
    });
    d->averageRtt = sum / d->rttMeasurements.size();

    Q_EMIT rttChanged();

    if (d->lastBandwithMeasurement < 0) {
        return;
    }

    if ((now - d->lastNetworkResult) < networkResultInterval) {
        return;
    }

    d->lastNetworkResult = now;

    d->rdpAutodetect->netCharBandwidth = d->lastBandwithMeasurement;
    d->rdpAutodetect->netCharBaseRTT = clk::duration_cast<clk::milliseconds>(d->minimumRtt).count();
    d->rdpAutodetect->netCharAverageRTT = clk::duration_cast<clk::milliseconds>(d->averageRtt).count();
    d->rdpAutodetect->NetworkCharacteristicsResult(d->rdpAutodetect->context, d->nextSequenceNumber());
}

uint32_t NetworkDetection::Private::nextSequenceNumber()
{
    auto sequence = sequenceNumber;
    while (sequence == 0 || rttRequests.contains(sequence)) {
        ++sequence;
    }
    sequenceNumber = sequence + 1;
    return sequence;
}

} // namespace KRdp

#include "moc_NetworkDetection.cpp"
