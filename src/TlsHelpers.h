/*
 *  SPDX-FileCopyrightText: 2026 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

#include <QSslCertificate>
#include <QSslKey>
#include <QString>

namespace KRdp
{

namespace TlsHelpers
{

template<typename T>
struct TlsResult {
    QByteArray pem;
    T value;
};

TlsResult<QSslKey> generatePrivateKey();
TlsResult<QSslCertificate> generateSelfSignedCertificate(const TlsResult<QSslKey> &privateKey, const QString &commonName, int notBeforeDays, int notAfterDays);

}
}
