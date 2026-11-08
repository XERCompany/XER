/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/cert/cert_verifier.h"

#include "content/network/logging/network_logger.h"

#include <ctime>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace XER::Network::Cert {

namespace {

std::string X509NameOneLine(X509_NAME* name) {
    if (!name) return {};
    char buf[256]{};
    X509_NAME_oneline(name, buf, sizeof(buf));
    return buf;
}

}  // namespace

bool CertVerifier::Verify(const std::string& pem_cert,
                          const std::string& hostname,
                          CertVerifyResult* result) const {
    result->is_valid = false;

    BIO* bio = BIO_new_mem_buf(pem_cert.data(),
                               static_cast<int>(pem_cert.size()));
    if (!bio) {
        result->error_message = "BIO_new_mem_buf failed";
        Logging::NetworkLogger::Instance().LogError("cert_verifier", result->error_message);
        return false;
    }

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!cert) {
        result->error_message = "PEM_read_bio_X509 failed";
        Logging::NetworkLogger::Instance().LogError("cert_verifier", result->error_message);
        return false;
    }

    result->subject = X509NameOneLine(X509_get_subject_name(cert));
    result->issuer  = X509NameOneLine(X509_get_issuer_name(cert));

    // Validity window
    const ASN1_TIME* nb = X509_get0_notBefore(cert);
    const ASN1_TIME* na = X509_get0_notAfter(cert);
    struct tm tm_nb{}, tm_na{};
    ASN1_TIME_to_tm(nb, &tm_nb);
    ASN1_TIME_to_tm(na, &tm_na);
#ifdef _WIN32
    result->not_before = _mkgmtime(&tm_nb);
    result->not_after  = _mkgmtime(&tm_na);
#else
    result->not_before = timegm(&tm_nb);
    result->not_after  = timegm(&tm_na);
#endif

    const std::time_t now = std::time(nullptr);
    if (now < result->not_before) {
        result->status = CertStatus::kNotYetValid;
        result->error_message = "certificate not yet valid";
        Logging::NetworkLogger::Instance().LogWarning("cert_verifier",
            "Certificate not yet valid for " + hostname);
        X509_free(cert);
        return false;
    }
    if (now > result->not_after) {
        result->status = CertStatus::kExpired;
        result->error_message = "certificate expired";
        Logging::NetworkLogger::Instance().LogWarning("cert_verifier",
            "Certificate expired for " + hostname);
        X509_free(cert);
        return false;
    }

    // Hostname check via SAN / CN
    bool hostname_ok = false;
    GENERAL_NAMES* sans = static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    if (sans) {
        for (int i = 0; i < sk_GENERAL_NAME_num(sans); ++i) {
            GENERAL_NAME* gn = sk_GENERAL_NAME_value(sans, i);
            if (gn->type == GEN_DNS) {
                const char* val = reinterpret_cast<const char*>(
                    ASN1_STRING_get0_data(gn->d.dNSName));
                result->san.emplace_back(val);
                if (!hostname_ok && hostname == val) hostname_ok = true;
            }
        }
        GENERAL_NAMES_free(sans);
    }
    if (!hostname_ok) {
        // fallback to CN
        const int idx = X509_NAME_get_index_by_NID(
            X509_get_subject_name(cert), NID_commonName, -1);
        if (idx >= 0) {
            X509_NAME_ENTRY* entry = X509_NAME_get_entry(
                X509_get_subject_name(cert), idx);
            const char* cn = reinterpret_cast<const char*>(
                ASN1_STRING_get0_data(X509_NAME_ENTRY_get_data(entry)));
            hostname_ok = (hostname == cn);
        }
    }

    if (!hostname_ok) {
        result->status = CertStatus::kHostnameMismatch;
        result->error_message = "hostname mismatch";
        Logging::NetworkLogger::Instance().LogWarning("cert_verifier",
            "Hostname mismatch for " + hostname);
        X509_free(cert);
        return false;
    }

    result->status   = CertStatus::kOk;
    result->is_valid = true;
    Logging::NetworkLogger::Instance().LogInfo("cert_verifier",
        "Certificate valid for " + hostname);
    X509_free(cert);
    return true;
}

// static
bool CertVerifier::IsTimeValid(const CertVerifyResult& result) {
    const std::time_t now = std::time(nullptr);
    return now >= result.not_before && now <= result.not_after;
}

}  // namespace XER::Network::Cert
