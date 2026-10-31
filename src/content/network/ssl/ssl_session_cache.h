/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <openssl/ssl.h>

namespace network::ssl {

// SSL session cache entry
struct SslSessionEntry {
    SSL_SESSION* session;
    std::string hostname;
    uint64_t created_at_ms;
    uint64_t expires_at_ms;
    std::string cipher_suite;
    std::string protocol_version;

    SslSessionEntry() : session(nullptr), created_at_ms(0), expires_at_ms(0) {}
};

// SSL session cache manager
class SslSessionCache {
public:
    static SslSessionCache& Instance();

    // Get a cached session for a hostname
    SSL_SESSION* GetSession(const std::string& hostname);

    // Store a session for a hostname
    void StoreSession(const std::string& hostname, SSL_SESSION* session);

    // Remove a session from cache
    void RemoveSession(const std::string& hostname);

    // Clear all sessions
    void Clear();

    // Get cache statistics
    size_t GetSessionCount() const;
    size_t GetTotalSize() const;

    // Set maximum cache size (number of sessions)
    void SetMaxSessions(size_t max_sessions) { max_sessions_ = max_sessions; }

    // Set session TTL (in seconds)
    void SetSessionTTL(uint64_t ttl_seconds) { session_ttl_seconds_ = ttl_seconds; }

private:
    SslSessionCache();
    ~SslSessionCache();

    SslSessionCache(const SslSessionCache&) = delete;
    SslSessionCache& operator=(const SslSessionCache&) = delete;

    // Check if entry is expired
    bool IsExpired(const SslSessionEntry& entry) const;

    // Remove expired entries
    void CleanupExpired();

    // Get current time in milliseconds
    static uint64_t NowMs();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SslSessionEntry> cache_;
    size_t max_sessions_;
    uint64_t session_ttl_seconds_;
};

// RAII wrapper for SSL session
class ScopedSslSession {
public:
    explicit ScopedSslSession(SSL_SESSION* session);
    ~ScopedSslSession();

    ScopedSslSession(ScopedSslSession&& other) noexcept;
    ScopedSslSession& operator=(ScopedSslSession&& other) noexcept;

    // Disable copy
    ScopedSslSession(const ScopedSslSession&) = delete;
    ScopedSslSession& operator=(const ScopedSslSession&) = delete;

    SSL_SESSION* get() const { return session_; }
    SSL_SESSION* operator->() const { return session_; }
    explicit operator bool() const { return session_ != nullptr; }

    void reset(SSL_SESSION* session = nullptr);

private:
    SSL_SESSION* session_;
};

} // namespace network::ssl
