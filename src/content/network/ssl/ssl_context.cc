/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/ssl/ssl_context.h"

#include <absl/strings/str_cat.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <openssl/x509err.h>
#endif

namespace network::ssl {
namespace {

#ifdef _WIN32
bool LoadWindowsRootCertificates(SSL_CTX* context, std::string* error) {
    X509_STORE* store = SSL_CTX_get_cert_store(context);
    if (store == nullptr) {
        if (error) *error = "Failed to get OpenSSL certificate store";
        return false;
    }

    constexpr DWORD kStoreLocations[] = {
        CERT_SYSTEM_STORE_CURRENT_USER,
        CERT_SYSTEM_STORE_LOCAL_MACHINE,
    };
    size_t loaded_certificates = 0;

    for (const DWORD location : kStoreLocations) {
        HCERTSTORE windows_store = CertOpenStore(
            CERT_STORE_PROV_SYSTEM_W,
            0,
            0,
            location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
            L"ROOT");
        if (windows_store == nullptr) {
            continue;
        }

        PCCERT_CONTEXT certificate_context = nullptr;
        while ((certificate_context = CertEnumCertificatesInStore(
                    windows_store, certificate_context)) != nullptr) {
            const unsigned char* encoded = certificate_context->pbCertEncoded;
            X509* certificate = d2i_X509(
                nullptr, &encoded,
                static_cast<long>(certificate_context->cbCertEncoded));
            if (certificate == nullptr) {
                ERR_clear_error();
                continue;
            }

            ERR_clear_error();
            if (X509_STORE_add_cert(store, certificate) == 1) {
                ++loaded_certificates;
            } else {
                const unsigned long add_error = ERR_peek_last_error();
                const bool duplicate = ERR_GET_LIB(add_error) == ERR_LIB_X509 &&
                    ERR_GET_REASON(add_error) == X509_R_CERT_ALREADY_IN_HASH_TABLE;
                if (duplicate) {
                    ERR_clear_error();
                } else {
                    if (error) *error = GetLastSslError();
                    X509_free(certificate);
                    CertCloseStore(windows_store, 0);
                    return false;
                }
            }

            X509_free(certificate);
        }

        CertCloseStore(windows_store, 0);
    }

    if (loaded_certificates == 0) {
        if (error) {
            *error = "No trusted root certificates were loaded from Windows";
        }
        return false;
    }

    return true;
}
#endif

}  // namespace

SslContext::SslContext()
    : ctx_(nullptr),
      verify_mode_(SSL_VERIFY_PEER),
      verify_depth_(4) {
}

SslContext::~SslContext() {
    Shutdown();
}

SslContext& SslContext::Instance() {
    static SslContext instance;
    return instance;
}

bool SslContext::Initialize(const std::string& ca_bundle_path, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ctx_ != nullptr) {
        if (error) *error = "SSL context already initialized";
        return false;
    }

    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // Create SSL context
    ctx_ = SSL_CTX_new(TLS_client_method());
    if (ctx_ == nullptr) {
        if (error) *error = GetLastSslError();
        return false;
    }

    // Set protocol version (TLS 1.2 and 1.3 only)
    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx_, TLS1_3_VERSION);

    // Set verify mode
    SSL_CTX_set_verify(ctx_, verify_mode_, nullptr);
    SSL_CTX_set_verify_depth(ctx_, verify_depth_);

    // Load CA certificates
    if (!ca_bundle_path.empty()) {
        ca_bundle_path_ = ca_bundle_path;
        if (!LoadCaCertificates(ca_bundle_path, error)) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
    } else {
#ifdef _WIN32
        if (!LoadWindowsRootCertificates(ctx_, error)) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
#else
        // Use default system certificates
        if (!SSL_CTX_set_default_verify_paths(ctx_)) {
            if (error) *error = "Failed to load default CA certificates: " + GetLastSslError();
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
#endif
    }

    // Set default options
    SSL_CTX_set_options(ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_options(ctx_, SSL_OP_NO_RENEGOTIATION);

    return true;
}

void SslContext::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ctx_ != nullptr) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }

    EVP_cleanup();
    ERR_free_strings();
}

bool SslContext::LoadCaCertificates(const std::string& path, std::string* error) {
    if (SSL_CTX_load_verify_locations(ctx_, path.c_str(), nullptr) != 1) {
        if (error) *error = "Failed to load CA bundle from " + path + ": " + GetLastSslError();
        return false;
    }
    return true;
}

void SslContext::SetVerifyMode(int mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    verify_mode_ = mode;
    if (ctx_) {
        SSL_CTX_set_verify(ctx_, mode, nullptr);
    }
}

void SslContext::SetVerifyDepth(int depth) {
    std::lock_guard<std::mutex> lock(mutex_);
    verify_depth_ = depth;
    if (ctx_) {
        SSL_CTX_set_verify_depth(ctx_, depth);
    }
}

void SslContext::SetCipherList(const std::string& cipher_list) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        SSL_CTX_set_cipher_list(ctx_, cipher_list.c_str());
    }
}

void SslContext::SetProtocolVersion(int min_version, int max_version) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        SSL_CTX_set_min_proto_version(ctx_, min_version);
        SSL_CTX_set_max_proto_version(ctx_, max_version);
    }
}

void SslContext::SetOptions(long options) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        SSL_CTX_set_options(ctx_, options);
    }
}

SSL* SslContext::CreateSsl() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_ == nullptr) {
        return nullptr;
    }
    return SSL_new(ctx_);
}

// ScopedSsl implementation
ScopedSsl::ScopedSsl(SSL* ssl) : ssl_(ssl) {
}

ScopedSsl::~ScopedSsl() {
    reset();
}

ScopedSsl::ScopedSsl(ScopedSsl&& other) noexcept : ssl_(other.ssl_) {
    other.ssl_ = nullptr;
}

ScopedSsl& ScopedSsl::operator=(ScopedSsl&& other) noexcept {
    if (this != &other) {
        reset();
        ssl_ = other.ssl_;
        other.ssl_ = nullptr;
    }
    return *this;
}

void ScopedSsl::reset(SSL* ssl) {
    if (ssl_ != nullptr) {
        SSL_free(ssl_);
    }
    ssl_ = ssl;
}

// ScopedSslCtx implementation
ScopedSslCtx::ScopedSslCtx(SSL_CTX* ctx) : ctx_(ctx) {
}

ScopedSslCtx::~ScopedSslCtx() {
    reset();
}

ScopedSslCtx::ScopedSslCtx(ScopedSslCtx&& other) noexcept : ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

ScopedSslCtx& ScopedSslCtx::operator=(ScopedSslCtx&& other) noexcept {
    if (this != &other) {
        reset();
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

void ScopedSslCtx::reset(SSL_CTX* ctx) {
    if (ctx_ != nullptr) {
        SSL_CTX_free(ctx_);
    }
    ctx_ = ctx;
}

// Utility functions
std::string GetSslErrorString(int error_code) {
    char buf[256];
    ERR_error_string_n(error_code, buf, sizeof(buf));
    return std::string(buf);
}

std::string GetLastSslError() {
    unsigned long error = ERR_get_error();
    if (error == 0) {
        return "No SSL error";
    }
    return GetSslErrorString(error);
}

} // namespace network::ssl
