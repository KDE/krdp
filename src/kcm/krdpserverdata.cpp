// SPDX-FileCopyrightText: 2026 Tobias Ozór <tobiasozor@outlook.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "krdpserverdata.h"
#include "krdpserversettings.h"

using namespace Qt::StringLiterals;

KRDPServerData::KRDPServerData(QObject *parent)
    : KCModuleData(parent)
    , m_settings(new KRDPServerSettings(this))
{
}

KRDPServerSettings *KRDPServerData::settings() const
{
    return m_settings;
}

bool KRDPServerData::isDefaults() const
{
    for (const auto *item : m_settings->items()) {
        if (item->key() == u"Users"_s || item->key() == u"Certificate"_s || item->key() == u"CertificateKey"_s) {
            continue;
        }
        if (!item->isDefault())
            return false;
    }
    return true;
}

#include "moc_krdpserverdata.cpp"
