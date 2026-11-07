/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace XER::network::ssl {

// SSL context manager using OpenSSL
class SslContext {
public:
    static SslContext& Instance();

    // Initialize SSL context
    bool Initialize(const std::string& ca_bundle_path, std::string* error);

    // Shutdown SSL context
    void Shutdown();

    // Get the SSL_CTX
    SSL_CTX* GetContext() const { return ctx_; }

    // Check if initialized
    bool IsInitialized() const { return ctx_ != nullptr; }

    // Configuration
    void SetVerifyMode(int mode);
    void SetVerifyDepth(int depth);
    void SetCipherList(const std::string& cipher_list);
    void SetProtocolVersion(int min_version, int max_version);
    void SetOptions(long options);

    // Create a new SSL object from this context
    SSL* CreateSsl();

    // Get CA bundle path
    const std::string& GetCaBundlePath() const { return ca_bundle_path_; }

private:
    SslContext();
    ~SslContext();

    SslContext(const SslContext&) = delete;
    SslContext& operator=(const SslContext&) = delete;

    // Load CA certificates
    bool LoadCaCertificates(const std::string& path, std::string* error);

    SSL_CTX* ctx_;
    std::string ca_bundle_path_;
    int verify_mode_;
    int verify_depth_;
    mutable std::mutex mutex_;
};

// RAII wrapper for SSL objects
class ScopedSsl {
public:
    ScopedSsl() : ssl_(nullptr) {}
    explicit ScopedSsl(SSL* ssl);
    ~ScopedSsl();

    ScopedSsl(ScopedSsl&& other) noexcept;
    ScopedSsl& operator=(ScopedSsl&& other) noexcept;

    // Disable copy
    ScopedSsl(const ScopedSsl&) = delete;
    ScopedSsl& operator=(const ScopedSsl&) = delete;

    SSL* get() const { return ssl_; }
    SSL* operator->() const { return ssl_; }
    explicit operator bool() const { return ssl_ != nullptr; }

    void reset(SSL* ssl = nullptr);

private:
    SSL* ssl_;
};

// RAII wrapper for SSL_CTX
class ScopedSslCtx {
public:
    explicit ScopedSslCtx(SSL_CTX* ctx);
    ~ScopedSslCtx();

    ScopedSslCtx(ScopedSslCtx&& other) noexcept;
    ScopedSslCtx& operator=(ScopedSslCtx&& other) noexcept;

    // Disable copy
    ScopedSslCtx(const ScopedSslCtx&) = delete;
    ScopedSslCtx& operator=(const ScopedSslCtx&) = delete;

    SSL_CTX* get() const { return ctx_; }
    SSL_CTX* operator->() const { return ctx_; }
    explicit operator bool() const { return ctx_ != nullptr; }

    void reset(SSL_CTX* ctx = nullptr);

private:
    SSL_CTX* ctx_;
};

// Utility functions for SSL errors
std::string GetSslErrorString(int error_code);
std::string GetLastSslError();

} // namespace XER::network::ssl
