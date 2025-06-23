// SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Clipboard.h"

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/server/cliprdr.h>

#include "PeerContext_p.h"
#include "RdpConnection.h"

#include "krdp_logging.h"

using namespace Qt::StringLiterals;

namespace KRdp
{

class KRDP_NO_EXPORT Clipboard::Private
{
public:
    using CliprdrServerContextPtr = std::unique_ptr<CliprdrServerContext, decltype(&cliprdr_server_context_free)>;

    Private(Clipboard *qq)
        : q(qq)
    {
    }

    Clipboard *q;

    uint32_t onClientFormatList(const CLIPRDR_FORMAT_LIST *formatList);
    uint32_t onClientFormatListResponse(const CLIPRDR_FORMAT_LIST_RESPONSE *formatListResponse);
    uint32_t onClientFormatDataRequest(const CLIPRDR_FORMAT_DATA_REQUEST *formatDataRequest);
    uint32_t onClientFormatDataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE *formatDataResponse);

    RdpConnection *session;

    CliprdrServerContextPtr clipContext = CliprdrServerContextPtr(nullptr, cliprdr_server_context_free);

    bool enabled = false;
    const QMimeData *serverData = nullptr;
    std::unique_ptr<QMimeData> clientData;

    static UINT clientFormatList(CliprdrServerContext *context, const CLIPRDR_FORMAT_LIST *formatList)
    {
        return reinterpret_cast<Clipboard *>(context->custom)->d->onClientFormatList(formatList);
    }

    static UINT clientFormatListResponse(CliprdrServerContext *context, const CLIPRDR_FORMAT_LIST_RESPONSE *formatListResponse)
    {
        return reinterpret_cast<Clipboard *>(context->custom)->d->onClientFormatListResponse(formatListResponse);
    }

    static UINT clientFormatDataRequest(CliprdrServerContext *context, const CLIPRDR_FORMAT_DATA_REQUEST *formatDataRequest)
    {
        return reinterpret_cast<Clipboard *>(context->custom)->d->onClientFormatDataRequest(formatDataRequest);
    }

    static UINT clientFormatDataResponse(CliprdrServerContext *context, const CLIPRDR_FORMAT_DATA_RESPONSE *formatDataResponse)
    {
        return reinterpret_cast<Clipboard *>(context->custom)->d->onClientFormatDataResponse(formatDataResponse);
    }
};

Clipboard::Clipboard(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>(this))
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

    d->clipContext->ClientFormatList = Private::clientFormatList;
    d->clipContext->ClientFormatListResponse = Private::clientFormatListResponse;
    d->clipContext->ClientFormatDataRequest = Private::clientFormatDataRequest;
    d->clipContext->ClientFormatDataResponse = Private::clientFormatDataResponse;

    // returns 0 on success
    // https://pub.freerdp.com/api/server_2cliprdr__main_8c.html#ab4e8a28c6b4371c2a5f34e8716ab1e9e
    if (d->clipContext->Start(d->clipContext.get())) {
        qCWarning(KRDP) << "Could not start Clipboard context";
        return false;
    };

    d->enabled = true;

    return true;
}

bool Clipboard::enabled()
{
    return d->enabled;
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
    d->enabled = false;
}

void Clipboard::setServerData(const QMimeData *data)
{
    if (d->serverData) {
        delete d->serverData;
    }

    d->serverData = data;
    QMetaObject::invokeMethod(this, &Clipboard::sendServerData, Qt::QueuedConnection);
}

std::unique_ptr<QMimeData> Clipboard::getClipboard() const
{
    return std::move(d->clientData);
}

void Clipboard::sendServerData()
{
    if (!d->serverData || !d->enabled) {
        return;
    }

    CLIPRDR_FORMAT_LIST formatList;
    formatList.common.msgType = CB_FORMAT_LIST;
    formatList.common.msgFlags = 0;
    formatList.numFormats = 1;
    formatList.formats = reinterpret_cast<CLIPRDR_FORMAT *>(malloc(sizeof(CLIPRDR_FORMAT)));
    formatList.formats[0].formatId = CF_UNICODETEXT;
    d->clipContext->ServerFormatList(d->clipContext.get(), &formatList);
}

uint32_t Clipboard::Private::onClientFormatList(const CLIPRDR_FORMAT_LIST *formatList)
{
    for (uint32_t i = 0; i < formatList->numFormats; ++i) {
        auto format = formatList->formats[i];

        switch (format.formatId) {
        case CF_TEXT:
        case CF_UNICODETEXT:
        case CF_OEMTEXT: {
            CLIPRDR_FORMAT_DATA_REQUEST formatDataRequest{.common = CLIPRDR_HEADER({.msgType = CB_FORMAT_DATA_REQUEST, .msgFlags = 0, .dataLen = 4}),
                                                          .requestedFormatId = CF_UNICODETEXT};
            clipContext->ServerFormatDataRequest(clipContext.get(), &formatDataRequest);
            break;
        }
        default:
            break;
        }
    }

    return CHANNEL_RC_OK;
}

uint32_t Clipboard::Private::onClientFormatListResponse(const CLIPRDR_FORMAT_LIST_RESPONSE *formatListResponse)
{
    return CHANNEL_RC_OK;
}

uint32_t Clipboard::Private::onClientFormatDataRequest(const CLIPRDR_FORMAT_DATA_REQUEST *formatDataRequest)
{
    if (!serverData || formatDataRequest->requestedFormatId != CF_UNICODETEXT) {
        return CHANNEL_RC_OK;
    }

    auto data = serverData->text().toStdU16String();

    CLIPRDR_FORMAT_DATA_RESPONSE response{
        .common = CLIPRDR_HEADER({.msgType = CB_FORMAT_DATA_RESPONSE, .msgFlags = CB_RESPONSE_OK, .dataLen = uint32_t(data.length()) * 2}),
        .requestedFormatData = reinterpret_cast<BYTE *>(data.data())};

    clipContext->ServerFormatDataResponse(clipContext.get(), &response);

    return CHANNEL_RC_OK;
}

uint32_t Clipboard::Private::onClientFormatDataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE *formatDataResponse)
{
    if (!(formatDataResponse->common.msgFlags & CB_RESPONSE_OK)) {
        return CHANNEL_RC_OK;
    }

    const auto nCharacters = formatDataResponse->common.dataLen / 2 - 1; // Each char16_t is 2 bytes, plus null terminator
    if (nCharacters < 0) {
        clientData.reset();
        Q_EMIT q->clientDataChanged();
        return CHANNEL_RC_OK; // empty string
    }

    clientData.reset(new QMimeData());

    clientData->setText(QString::fromUtf16(reinterpret_cast<const char16_t *>(formatDataResponse->requestedFormatData), nCharacters));
    Q_EMIT q->clientDataChanged();

    return CHANNEL_RC_OK;
}
}
