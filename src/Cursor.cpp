// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "Cursor.h"

#include <QHash>

#include <freerdp/freerdp.h>
#include <freerdp/peer.h>

#include "RdpConnection.h"

using namespace KRdp;

static QByteArray createXorMask(const QImage &image)
{
    auto converted = image.convertToFormat(QImage::Format_ARGB32);
    converted.mirror(false, true);
    converted.rgbSwap();
    return QByteArray(reinterpret_cast<char *>(converted.bits()), converted.sizeInBytes());
}

bool Cursor::CursorUpdate::operator==(const Cursor::CursorUpdate &other) const
{
    return hotspot == other.hotspot && image == other.image;
}

class KRDP_NO_EXPORT Cursor::Private
{
public:
    RdpConnection *session;

    CursorType cursorType = CursorType::SystemDefault;

    CursorUpdate *lastUsedCursor = nullptr;

    QHash<uint32_t, CursorUpdate> cursorCache;
};

Cursor::Cursor(RdpConnection *session)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->session = session;
}

Cursor::~Cursor()
{
}

void Cursor::update(const Cursor::CursorUpdate &update)
{
    if (d->session->state() != RdpConnection::State::Streaming) {
        return;
    }

    // Ignore updates with no image
    if (update.image.isNull()) {
        return;
    }

    auto image = update.image;
    // RDP cannot handle cursor images larger than 384x384 px. If we get such an
    // image, discard it and use the system default cursor instead.
    if (image.width() > 384 || image.height() > 384) {
        setCursorType(CursorType::SystemDefault);
        return;
    }

    // Show a bitmap image for the cursor.
    setCursorType(CursorType::Image);

    // If we're already showing it, simply update the last used timestamp and
    // do nothing else.
    if (d->lastUsedCursor && *d->lastUsedCursor == update) {
        d->lastUsedCursor->lastUsed = std::chrono::steady_clock::now();
        return;
    }

    auto context = d->session->rdpPeerContext();
    auto updatePointer = d->session->rdpPeerContext()->update->pointer;

    // Cursor images are cached. Check to see if the newly requested cursor is
    // already in the cache, and if so, mark that as the current cursor.
    auto itr = std::find_if(d->cursorCache.begin(), d->cursorCache.end(), [&update](const CursorUpdate &cached) {
        return update == cached;
    });
    if (itr != d->cursorCache.end()) {
        d->lastUsedCursor = &itr.value();
        itr->lastUsed = std::chrono::steady_clock::now();
        POINTER_CACHED_UPDATE pointerCachedUpdate;
        pointerCachedUpdate.cacheIndex = itr->cacheId;
        updatePointer->PointerCached(context, &pointerCachedUpdate);
        return;
    }

    // We have a completely new cursor, let's add the required metadata for the
    // cache.
    CursorUpdate newCursor;
    newCursor.hotspot = update.hotspot;
    newCursor.image = update.image;
    newCursor.cacheId = d->cursorCache.size();
    newCursor.lastUsed = std::chrono::steady_clock::now();

    // Evict least recently used cursor from the cache if it has grown too large.
    if (d->cursorCache.size() >= freerdp_settings_get_uint32(d->session->rdpPeerContext()->settings, FreeRDP_PointerCacheSize)) {
        auto lru = std::min_element(d->cursorCache.cbegin(), d->cursorCache.cend(), [](const CursorUpdate &first, const CursorUpdate &second) {
            return first.lastUsed < second.lastUsed;
        });
        newCursor.cacheId = lru->cacheId;
        d->cursorCache.erase(lru);
    }

    if (update.image.width() < 96 && update.image.height() < 96) {
        POINTER_NEW_UPDATE pointerNewUpdate;
        pointerNewUpdate.xorBpp = 32;
        auto &colorUpdate = pointerNewUpdate.colorPtrAttr;
        colorUpdate.cacheIndex = newCursor.cacheId;
        colorUpdate.hotSpotX = newCursor.hotspot.x();
        colorUpdate.hotSpotY = newCursor.hotspot.y();
        colorUpdate.width = newCursor.image.width();
        colorUpdate.height = newCursor.image.height();
        colorUpdate.lengthAndMask = 0;
        colorUpdate.andMaskData = nullptr;
        auto xorMask = createXorMask(newCursor.image);
        colorUpdate.lengthXorMask = xorMask.size();
        colorUpdate.xorMaskData = reinterpret_cast<BYTE *>(xorMask.data());
        updatePointer->PointerNew(context, &pointerNewUpdate);
    } else {
        POINTER_LARGE_UPDATE pointerLargeUpdate;
        pointerLargeUpdate.xorBpp = 32;
        pointerLargeUpdate.cacheIndex = newCursor.cacheId;
        pointerLargeUpdate.hotSpotX = newCursor.hotspot.x();
        pointerLargeUpdate.hotSpotY = newCursor.hotspot.y();
        pointerLargeUpdate.width = newCursor.image.width();
        pointerLargeUpdate.height = newCursor.image.height();
        pointerLargeUpdate.lengthAndMask = 0;
        pointerLargeUpdate.andMaskData = nullptr;
        auto xorMask = createXorMask(newCursor.image);
        pointerLargeUpdate.lengthXorMask = xorMask.size();
        pointerLargeUpdate.xorMaskData = reinterpret_cast<BYTE *>(xorMask.data());
        updatePointer->PointerLarge(context, &pointerLargeUpdate);
    }

    POINTER_CACHED_UPDATE pointerCachedUpdate;
    pointerCachedUpdate.cacheIndex = newCursor.cacheId;
    updatePointer->PointerCached(context, &pointerCachedUpdate);

    // Actually insert the new cursor into the cache.
    auto inserted = d->cursorCache.insert(newCursor.cacheId, newCursor);
    d->lastUsedCursor = &inserted.value();
}

void Cursor::setCursorType(Cursor::CursorType type)
{
    if (d->cursorType == type) {
        return;
    }

    d->cursorType = type;

    if (type != CursorType::Image) {
        d->lastUsedCursor = nullptr;
        POINTER_SYSTEM_UPDATE pointerSystemUpdate;
        pointerSystemUpdate.type = type == CursorType::Hidden ? SYSPTR_NULL : SYSPTR_DEFAULT;
        d->session->rdpPeerContext()->update->pointer->PointerSystem(d->session->rdpPeerContext(), &pointerSystemUpdate);
    }
}

#include "moc_Cursor.cpp"
