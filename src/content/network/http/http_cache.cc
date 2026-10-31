/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_cache.h"

#include "content/network/cache/network_cache_manager.h"
#include "content/network/logging/network_logger.h"

#include <chrono>

namespace network::http {

HttpCache& HttpCache::Instance() {
    static HttpCache inst;
    return inst;
}

bool HttpCache::Lookup(const std::string& key, CachedResponse* out) const {
    // Try NetworkCacheManager first
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    std::string error;
    auto cached_entry = cache_mgr.GetCachedHttpResponse(key, &error);

    if (cached_entry) {
        logging::NetworkLogger::Instance().LogCacheHit("http_response", key);

        if (out) {
            out->status_code = cached_entry->status_code;
            out->headers = cached_entry->headers;
            out->body = cached_entry->body;
            out->expires = cached_entry->cached_at + cached_entry->ttl;
        }
        return true;
    }

    logging::NetworkLogger::Instance().LogCacheMiss("http_response", key);

    // Fallback to in-memory cache
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = store_.find(key);
    if (it == store_.end()) return false;
    if (std::chrono::steady_clock::now() > it->second.expires) return false;
    if (out) *out = it->second;
    return true;
}

void HttpCache::Store(const std::string& key, CachedResponse entry) {
    // Store in NetworkCacheManager
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    cache::HttpResponseCacheEntry cache_entry;
    cache_entry.status_code = entry.status_code;
    cache_entry.headers = entry.headers;
    cache_entry.body = entry.body;
    cache_entry.cached_at = std::chrono::system_clock::now();

    // Calculate TTL from expires time
    auto now = std::chrono::steady_clock::now();
    auto ttl = std::chrono::duration_cast<std::chrono::seconds>(entry.expires - now);
    cache_entry.ttl = ttl > std::chrono::seconds(0) ? ttl : std::chrono::seconds(300);  // Default 5 min

    std::string error;
    if (cache_mgr.CacheHttpResponse(key, cache_entry, &error)) {
        logging::NetworkLogger::Instance().LogCacheStore("http_response", key, entry.body.size());
    }

    // Also store in in-memory cache as backup
    std::lock_guard<std::mutex> lk(mu_);
    store_[key] = std::move(entry);
}

void HttpCache::Invalidate(const std::string& key) {
    // Invalidate in NetworkCacheManager
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    cache_mgr.InvalidateHttpResponse(key);
    logging::NetworkLogger::Instance().LogCacheInvalidate("http_response", key);

    // Invalidate in in-memory cache
    std::lock_guard<std::mutex> lk(mu_);
    store_.erase(key);
}

void HttpCache::Clear() {
    // Clear NetworkCacheManager HTTP responses
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    cache_mgr.ClearHttpResponses();

    // Clear in-memory cache
    std::lock_guard<std::mutex> lk(mu_);
    store_.clear();
}

}  // namespace network::http
