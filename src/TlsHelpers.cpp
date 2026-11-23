/*
 *  SPDX-FileCopyrightText: 2026 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

#include <QSslSocket>

extern "C" {
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
}

#include "krdp_logging.h"

#include "TlsHelpers.h"

using namespace Qt::StringLiterals;

using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, decltype([](EVP_PKEY *p) {
                                         EVP_PKEY_free(p);
                                     })>;
using bio_ptr = std::unique_ptr<BIO, decltype([](BIO *p) {
                                    BIO_free_all(p);
                                })>;
using x509_ptr = std::unique_ptr<X509, decltype([](X509 *p) {
                                     X509_free(p);
                                 })>;
using x509_name_ptr = std::unique_ptr<X509_NAME, decltype([](X509_NAME *p) {
                                          X509_NAME_free(p);
                                      })>;
using x509_extension_ptr = std::unique_ptr<X509_EXTENSION, decltype([](X509_EXTENSION *p) {
                                               X509_EXTENSION_free(p);
                                           })>;
using bn_ptr = std::unique_ptr<BIGNUM, decltype([](BIGNUM *p) {
                                   BN_free(p);
                               })>;

QString getSslError()
{
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

namespace KRdp
{

namespace TlsHelpers
{

TlsResult<QSslKey> generatePrivateKey()
{
    auto fail = [](const char *what) -> TlsResult<QSslKey> {
        qCWarning(KRDP) << "Generation of private key failed in" << what << "with SSL error:" << getSslError();
        return {};
    };

    // Generate private key
    evp_pkey_ptr pKey(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"));
    if (!pKey) {
        return fail("generating private key");
    }

    // Convert to PEM for QSslKey
    bio_ptr bio(BIO_new(BIO_s_mem()));
    if (!bio) {
        return fail("allocating memory BIO");
    }

    if (!PEM_write_bio_PrivateKey(bio.get(), pKey.get(), nullptr, nullptr, 0, nullptr, nullptr)) {
        return fail("encoding private key as PEM");
    }

    BUF_MEM *mem = nullptr;
    if (!BIO_get_mem_ptr(bio.get(), &mem)) {
        return fail("retrieving PEM buffer");
    }

    const QByteArray pem(mem->data, mem->length);
    return {pem, QSslKey(pem, QSsl::Ec, QSsl::Pem, QSsl::PrivateKey)};
}

TlsResult<QSslCertificate> generateSelfSignedCertificate(const TlsResult<QSslKey> &privateKey, const QString &commonName, int notBeforeDays, int notAfterDays)
{
    auto fail = [](const char *what) -> TlsResult<QSslCertificate> {
        qCWarning(KRDP) << "Generation of self-signed certificate failed in" << what << "with SSL error:" << getSslError();
        return {{}, QSslCertificate()};
    };

    // Create certificate
    x509_ptr x509(X509_new());
    if (!x509) {
        return fail("allocating certificate");
    }

    // Set certificate version
    if (!X509_set_version(x509.get(), X509_VERSION_3)) {
        return fail("setting certificate version");
    }

    // Generate a random serial number for the certificate
    bn_ptr sn(BN_new());
    if (!sn) {
        return fail("allocating big number structure");
    }

    if (!BN_rand(sn.get(), 159, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY)) { // per RFC 5280
        return fail("generating random serial number");
    }

    if (!BN_to_ASN1_INTEGER(sn.get(), X509_get_serialNumber(x509.get()))) {
        return fail("setting certificate serial number from big number");
    }

    // Set common name for the certificate
    const QByteArray cn = commonName.toUtf8();
    x509_name_ptr name(X509_NAME_new());
    if (!name) {
        return fail("allocating name");
    }

    if (!X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char *>(cn.constData()), cn.size(), -1, 0)) {
        return fail("adding common name");
    }

    if (!X509_set_subject_name(x509.get(), name.get())) {
        return fail("setting certificate subject name");
    }

    if (!X509_set_issuer_name(x509.get(), name.get())) {
        return fail("setting certificate issuer name");
    }

    // Set validity for the certificate
    time_t now = time(nullptr);

    if (!X509_time_adj_ex(X509_getm_notBefore(x509.get()), notBeforeDays, 0, &now)) {
        return fail("setting not before time");
    }

    if (!X509_time_adj_ex(X509_getm_notAfter(x509.get()), notAfterDays, 0, &now)) {
        return fail("setting not after time");
    }

    // Add certificate extensions
    auto addExtension = [](X509 *cert, int nid, const char *value) {
        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

        x509_extension_ptr ext(X509V3_EXT_conf_nid(nullptr, &ctx, nid, value));
        if (!ext) {
            return false;
        }

        return X509_add_ext(cert, ext.get(), -1) == 1;
    };

    if (!addExtension(x509.get(), NID_basic_constraints, "critical,CA:FALSE")) {
        return fail("adding basicContraints extension");
    }

    if (!addExtension(x509.get(), NID_key_usage, "critical,digitalSignature")) {
        return fail("adding basicContraints extension");
    }

    if (!addExtension(x509.get(), NID_ext_key_usage, "serverAuth")) {
        return fail("adding basicContraints extension");
    }

    // Get the private key to sign certificate from QSslKey's PEM
    evp_pkey_ptr pKey;

    bio_ptr keyBio(BIO_new_mem_buf(privateKey.pem.constData(), privateKey.pem.size()));
    if (!keyBio) {
        return fail("allocating private key memory BIO");
    }

    pKey.reset(PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr));
    if (!pKey) {
        return fail("decoding PEM private key");
    }

    // Sign the certificate
    if (!X509_set_pubkey(x509.get(), pKey.get())) { // note: EVP_PKEY is a key pair and this uses the public part
        return fail("setting certificate public key");
    }

    if (!X509_sign(x509.get(), pKey.get(), EVP_sha256())) {
        return fail("signing certificate");
    }

    // Convert to PEM for QSslCertificate
    bio_ptr certBio(BIO_new(BIO_s_mem()));
    if (!certBio) {
        return fail("allocating certificate memory BIO");
    }

    if (!PEM_write_bio_X509(certBio.get(), x509.get())) {
        return fail("encoding certificate as PEM");
    }

    BUF_MEM *mem;
    if (!BIO_get_mem_ptr(certBio.get(), &mem)) {
        return fail("retrieving PEM buffer");
    }

    const QByteArray pem = QByteArray(mem->data, mem->length);
    return {pem, QSslCertificate(pem, QSsl::Pem)};
}

}
}
