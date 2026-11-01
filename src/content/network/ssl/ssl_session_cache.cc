/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/ssl/ssl_session_cache.h"

#include "content/network/logging/network_logger.h"

#include <chrono>

namespace network::ssl {

SslSessionCache::SslSessionCache()
    : max_sessions_(100),
      session_ttl_seconds_(3600) {  // 1 hour default
}

SslSessionCache::~SslSessionCache() {
    Clear();
}

SslSessionCache& SslSessionCache::Instance() {
    static SslSessionCache instance;
    return instance;
}

SSL_SESSION* SslSessionCache::GetSession(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Cleanup expired entries first
    CleanupExpired();

    auto it = cache_.find(hostname);
    if (it == cache_.end()) {
        logging::NetworkLogger::Instance().LogCacheMiss("ssl_session", hostname);
        return nullptr;
    }

    if (IsExpired(it->second)) {
        SSL_SESSION_free(it->second.session);
        cache_.erase(it);
        logging::NetworkLogger::Instance().LogCacheMiss("ssl_session", hostname);
        return nullptr;
    }

    // Increment reference count before returning
    SSL_SESSION_up_ref(it->second.session);
    logging::NetworkLogger::Instance().LogCacheHit("ssl_session", hostname);

    return it->second.session;
}

void SslSessionCache::StoreSession(const std::string& hostname, SSL_SESSION* session) {
    if (!session) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Remove existing entry if present
    auto it = cache_.find(hostname);
    if (it != cache_.end()) {
        SSL_SESSION_free(it->second.session);
        cache_.erase(it);
    }

    // Enforce max size limit
    if (cache_.size() >= max_sessions_) {
        // Remove oldest entry (simple LRU)
        auto oldest = cache_.begin();
        SSL_SESSION_free(oldest->second.session);
        cache_.erase(oldest);
    }

    // Create new entry
    SslSessionEntry entry;
    entry.session = session;
    entry.hostname = hostname;
    entry.created_at_ms = NowMs();
    entry.expires_at_ms = entry.created_at_ms + (session_ttl_seconds_ * 1000);

    // Get session info
    const SSL_CIPHER* cipher = SSL_SESSION_get0_cipher(session);
    if (cipher) {
        entry.cipher_suite = SSL_CIPHER_get_name(cipher);
    }

    int version = SSL_SESSION_get_protocol_version(session);
    entry.protocol_version = (version != 0) ? std::to_string(version) : "unknown";

    // Increment reference count for storage
    SSL_SESSION_up_ref(session);

    cache_[hostname] = entry;

    logging::NetworkLogger::Instance().LogCacheStore("ssl_session", hostname, 0);
}

void SslSessionCache::RemoveSession(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_.find(hostname);
    if (it != cache_.end()) {
        SSL_SESSION_free(it->second.session);
        cache_.erase(it);
        logging::NetworkLogger::Instance().LogCacheInvalidate("ssl_session", hostname);
    }
}

void SslSessionCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& pair : cache_) {
        SSL_SESSION_free(pair.second.session);
    }
    cache_.clear();
}

size_t SslSessionCache::GetSessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

size_t SslSessionCache::GetTotalSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (const auto& pair : cache_) {
        // Approximate ticket size: use i2d_SSL_SESSION length as proxy.
        unsigned char* buf = nullptr;
        int len = i2d_SSL_SESSION(pair.second.session, &buf);
        if (len > 0) {
            total += static_cast<size_t>(len);
            OPENSSL_free(buf);
        }
    }
    return total;
}

bool SslSessionCache::IsExpired(const SslSessionEntry& entry) const {
    return NowMs() > entry.expires_at_ms;
}

void SslSessionCache::CleanupExpired() {
    auto now = NowMs();
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (now > it->second.expires_at_ms) {
            SSL_SESSION_free(it->second.session);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t SslSessionCache::NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// ScopedSslSession implementation
ScopedSslSession::ScopedSslSession(SSL_SESSION* session) : session_(session) {
}

ScopedSslSession::~ScopedSslSession() {
    reset();
}

ScopedSslSession::ScopedSslSession(ScopedSslSession&& other) noexcept
    : session_(other.session_) {
    other.session_ = nullptr;
}

ScopedSslSession& ScopedSslSession::operator=(ScopedSslSession&& other) noexcept {
    if (this != &other) {
        reset();
        session_ = other.session_;
        other.session_ = nullptr;
    }
    return *this;
}

void ScopedSslSession::reset(SSL_SESSION* session) {
    if (session_ != nullptr) {
        SSL_SESSION_free(session_);
    }
    session_ = session;
}

} // namespace network::ssl
