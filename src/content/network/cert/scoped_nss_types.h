/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <memory>

namespace XER::Network::Cert {

struct X509Deleter    { void operator()(X509* p)    const { X509_free(p); } };
struct X509StoreDeleter { void operator()(X509_STORE* p) const { X509_STORE_free(p); } };
struct SSLDeleter     { void operator()(SSL* p)     const { SSL_free(p); } };
struct SSLCtxDeleter  { void operator()(SSL_CTX* p) const { SSL_CTX_free(p); } };
struct BIODeleter     { void operator()(BIO* p)     const { BIO_free(p); } };

using ScopedX509      = std::unique_ptr<X509, X509Deleter>;
using ScopedX509Store = std::unique_ptr<X509_STORE, X509StoreDeleter>;
using ScopedSSL       = std::unique_ptr<SSL, SSLDeleter>;
using ScopedSSLCtx    = std::unique_ptr<SSL_CTX, SSLCtxDeleter>;
using ScopedBIO       = std::unique_ptr<BIO, BIODeleter>;

}  // namespace XER::Network::Cert
