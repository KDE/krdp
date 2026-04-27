// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <memory>
#include <optional>

#include <QObject>
#include <QPoint>
#include <QRegion>
#include <QSize>

#include <PipeWireEncodedStream>
#include <PipeWireSourceStream>
#include <freerdp/server/rdpgfx.h>

#include "VideoFrame.h"
#include "krdp_export.h"

namespace KRdp
{

class RdpConnection;

/**
 * A class that encapsulates an RdpGfx video stream.
 *
 * Video streaming is done using the "RDP Graphics Pipeline" protocol
 * extension which allows using h264 as the codec for the video stream.
 * However, this protocol extension is fairly complex to setup and use.
 *
 * VideoStream makes sure to handle most of the complexity of the RdpGfx
 * protocol like ensuring the client knows the right resolution and a
 * surface at the right size. It also takes care of sending the frames,
 * using a separate thread for a submission queue.
 *
 * VideoStream is managed by Session. Each session will have one instance
 * of this class.
 */
class KRDP_EXPORT VideoStream : public QObject
{
    Q_OBJECT

public:
    enum class EncodingMode {
        H264,
        Progressive,
    };

    explicit VideoStream(RdpConnection *session);
    ~VideoStream() override;

    static EncodingMode configuredEncodingMode();

    bool initialize();
    void close();
    Q_SIGNAL void closed();
    Q_SIGNAL void cursorChanged(const PipeWireCursor &cursor);

    /**
     * Queue a frame to be sent to the client.
     *
     * This will add the provided frame to the queue of frames that should
     * be sent to the client.
     *
     * \param frame The frame to send.
     */
    void queueFrame(const VideoFrame &frame);

    /**
     * Indicate that the video state should be reset.
     *
     * This means the screen resolution and other information of the client
     * will be updated based on the current state of the VideoStream.
     */
    void reset();

    /**
     */
    bool enabled() const;
    void setEnabled(bool enabled);
    Q_SIGNAL void enabledChanged();
    void setStreamingEnabled(bool enabled);
    void setVideoQuality(quint8 quality);
    void setPipeWireSource(quint32 nodeId, int fd = -1);

    bool openChannel();

private:
    friend BOOL gfxChannelIdAssigned(RdpgfxServerContext *, uint32_t);
    friend uint32_t gfxCapsAdvertise(RdpgfxServerContext *, const RDPGFX_CAPS_ADVERTISE_PDU *);
    friend uint32_t gfxFrameAcknowledge(RdpgfxServerContext *, const RDPGFX_FRAME_ACKNOWLEDGE_PDU *);

    bool onChannelIdAssigned(uint32_t channelId);
    uint32_t onCapsAdvertise(const RDPGFX_CAPS_ADVERTISE_PDU *capsAdvertise);
    uint32_t onFrameAcknowledge(const RDPGFX_FRAME_ACKNOWLEDGE_PDU *frameAcknowledge);

    void onPacketReceived(const PipeWireEncodedStream::Packet &data);
    void onFrameReceived(const PipeWireFrame &frame);
    void destroySurface();
    void performReset(QSize size);
    bool hasInFlightCapacity() const;
    void sendFrame(const VideoFrame &frame);
    void sendFrameH264(const VideoFrame &frame);
    void sendFrameProgressive(const VideoFrame &frame);

    void updateRequestedFrameRate();

    class Private;
    const std::unique_ptr<Private> d;
};

}
