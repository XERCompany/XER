/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/cert/nss_certificate.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace XER::Network::Cert {

NssCertificate::NssCertificate(std::vector<uint8_t> der)
    : der_(std::move(der)) {}

std::string NssCertificate::ToPEM() const {
    if (der_.empty()) return {};
    const uint8_t* p = der_.data();
    X509* cert = d2i_X509(nullptr, &p, static_cast<long>(der_.size()));
    if (!cert) return {};
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(bio, &bptr);
    std::string pem(bptr->data, bptr->length);
    BIO_free(bio);
    X509_free(cert);
    return pem;
}

// static
NssCertificate NssCertificate::FromPEM(const std::string& pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return {};
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) return {};
    int len = i2d_X509(cert, nullptr);
    if (len <= 0) { X509_free(cert); return {}; }
    std::vector<uint8_t> der(static_cast<size_t>(len));
    uint8_t* p = der.data();
    i2d_X509(cert, &p);
    X509_free(cert);
    return NssCertificate(std::move(der));
}

}  // namespace XER::Network::Cert
