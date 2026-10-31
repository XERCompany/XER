/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "cache/cache_manager.h"
#include "content/network/http/http_response_headers.h"

namespace network::cache {

// HTTP response cache entry
struct HttpResponseCacheEntry {
    int status_code;
    http::HttpResponseHeaders headers;
    std::vector<uint8_t> body;
    std::chrono::system_clock::time_point cached_at;
    std::chrono::seconds ttl;
};

// DNS cache entry
struct DnsCacheEntry {
    std::vector<std::string> ip_addresses;
    std::chrono::system_clock::time_point cached_at;
    std::chrono::seconds ttl;
};

// SSL session cache entry
struct SslSessionCacheEntry {
    std::string session_data;  // Serialized SSL session
    std::chrono::system_clock::time_point cached_at;
    std::chrono::seconds ttl;
};

// Network cache manager - integrates with Engine::Cache
class NetworkCacheManager {
public:
    static NetworkCacheManager& Instance();

    // HTTP Response Cache
    bool CacheHttpResponse(const std::string& url,
                          const HttpResponseCacheEntry& response,
                          std::string* error);
    std::optional<HttpResponseCacheEntry> GetCachedHttpResponse(const std::string& url,
                                                                std::string* error);
    void InvalidateHttpResponse(const std::string& url);
    void ClearHttpResponses();

    // DNS Cache
    bool CacheDnsEntry(const std::string& hostname,
                      const DnsCacheEntry& entry,
                      std::string* error);
    std::optional<DnsCacheEntry> GetCachedDnsEntry(const std::string& hostname,
                                                  std::string* error);
    void InvalidateDnsEntry(const std::string& hostname);
    void ClearDnsCache();

    // SSL Session Cache
    bool CacheSslSession(const std::string& host,
                        const SslSessionCacheEntry& entry,
                        std::string* error);
    std::optional<SslSessionCacheEntry> GetCachedSslSession(const std::string& host,
                                                             std::string* error);
    void InvalidateSslSession(const std::string& host);
    void ClearSslSessions();

    // Cache statistics
    size_t GetHttpResponseCount() const;
    size_t GetDnsEntryCount() const;
    size_t GetSslSessionCount() const;

    // Configuration
    void SetDefaultHttpTtl(std::chrono::seconds ttl) { default_http_ttl_ = ttl; }
    void SetDefaultDnsTtl(std::chrono::seconds ttl) { default_dns_ttl_ = ttl; }
    void SetDefaultSslTtl(std::chrono::seconds ttl) { default_ssl_ttl_ = ttl; }

    std::chrono::seconds GetDefaultHttpTtl() const { return default_http_ttl_; }
    std::chrono::seconds GetDefaultDnsTtl() const { return default_dns_ttl_; }
    std::chrono::seconds GetDefaultSslTtl() const { return default_ssl_ttl_; }

private:
    NetworkCacheManager();
    ~NetworkCacheManager() = default;

    NetworkCacheManager(const NetworkCacheManager&) = delete;
    NetworkCacheManager& operator=(const NetworkCacheManager&) = delete;

    // Helper to generate cache keys
    std::string HttpCacheKey(const std::string& url) const;
    std::string DnsCacheKey(const std::string& hostname) const;
    std::string SslCacheKey(const std::string& host) const;

    // Helper to check if entry is expired
    template<typename EntryType>
    bool IsEntryExpired(const EntryType& entry) const;

    // Helper to serialize/deserialize
    bool SerializeHttpResponse(const HttpResponseCacheEntry& entry,
                               std::vector<uint8_t>* out,
                               std::string* error);
    bool DeserializeHttpResponse(const std::vector<uint8_t>& data,
                                 HttpResponseCacheEntry* out,
                                 std::string* error);

    Engine::Cache::CacheManager& cache_manager_;
    std::chrono::seconds default_http_ttl_ = std::chrono::seconds(300);  // 5 minutes
    std::chrono::seconds default_dns_ttl_ = std::chrono::seconds(600);   // 10 minutes
    std::chrono::seconds default_ssl_ttl_ = std::chrono::seconds(3600);  // 1 hour
};

} // namespace network::cache
