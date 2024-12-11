// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Clipboard.h"

#include "PeerContext_p.h"
#include "RdpConnection.h"
#include <QObject>
#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/server/cliprdr.h>

#include "krdp_logging.h"

namespace KRdp
{
class KRDP_NO_EXPORT Clipboard::Private
{
public:
    using CliprdrServerContextPtr = std::unique_ptr<CliprdrServerContext, decltype(&cliprdr_server_context_free)>;

    RdpConnection *session;

    CliprdrServerContextPtr clipContext = CliprdrServerContextPtr(nullptr, cliprdr_server_context_free);
};

Clipboard::Clipboard(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
}

Clipboard::~Clipboard()
{
}

bool Clipboard::initialize()
{
    if (d->clipContext) {
        return true;
    }

    auto peerContext = reinterpret_cast<PeerContext *>(d->session->rdpPeer()->context);

    d->clipContext = Private::CliprdrServerContextPtr{cliprdr_server_context_new(peerContext->virtualChannelManager), cliprdr_server_context_free};
    if (!d->clipContext) {
        qCWarning(KRDP) << "Failed creating Clipboard context";
        return false;
    }

    d->clipContext->useLongFormatNames = FALSE;
    d->clipContext->streamFileClipEnabled = FALSE;
    d->clipContext->fileClipNoFilePaths = FALSE;
    d->clipContext->canLockClipData = FALSE;
    d->clipContext->hasHugeFileSupport = FALSE;

    d->clipContext->custom = this;
    d->clipContext->rdpcontext = d->session->rdpPeer()->context;

    // returns 0 on success
    // https://pub.freerdp.com/api/server_2cliprdr__main_8c.html#ab4e8a28c6b4371c2a5f34e8716ab1e9e
    if (d->clipContext->Start(d->clipContext.get())) {
        qCWarning(KRDP) << "Could not start Clipboard context";
        return false;
    };

    m_enabled = true;

    return true;
}

bool Clipboard::enabled()
{
    return m_enabled;
}

void Clipboard::close()
{
    if (!d->clipContext) {
        return;
    }

    if (d->clipContext->Stop(d->clipContext.get())) {
        qCWarning(KRDP) << "Could not stop Clipboard context";
        return;
    };
    m_enabled = false;
}
}
