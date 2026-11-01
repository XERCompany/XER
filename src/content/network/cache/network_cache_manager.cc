/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/cache/network_cache_manager.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_split.h>

namespace network::cache {

NetworkCacheManager::NetworkCacheManager()
    : cache_manager_(Engine::Cache::CacheManager::Instance()) {
}

NetworkCacheManager& NetworkCacheManager::Instance() {
    static NetworkCacheManager instance;
    return instance;
}

std::string NetworkCacheManager::HttpCacheKey(const std::string& url) const {
    return absl::StrCat("http_response/", url);
}

std::string NetworkCacheManager::DnsCacheKey(const std::string& hostname) const {
    return absl::StrCat("dns/", hostname);
}

std::string NetworkCacheManager::SslCacheKey(const std::string& host) const {
    return absl::StrCat("ssl_session/", host);
}

template<typename EntryType>
bool NetworkCacheManager::IsEntryExpired(const EntryType& entry) const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.cached_at);
    return elapsed >= entry.ttl;
}

bool NetworkCacheManager::CacheHttpResponse(const std::string& url,
                                           const HttpResponseCacheEntry& response,
                                           std::string* error) {
    std::vector<uint8_t> serialized;
    if (!SerializeHttpResponse(response, &serialized, error)) {
        return false;
    }

    return cache_manager_.WritePersistentBinary(
        "network",
        HttpCacheKey(url),
        "response",
        serialized,
        error);
}

std::optional<HttpResponseCacheEntry> NetworkCacheManager::GetCachedHttpResponse(
    const std::string& url,
    std::string* error) {

    std::vector<uint8_t> data;
    if (!cache_manager_.ReadPersistentBinary(
            "network",
            HttpCacheKey(url),
            "response",
            &data,
            error)) {
        return std::nullopt;
    }

    HttpResponseCacheEntry entry;
    if (!DeserializeHttpResponse(data, &entry, error)) {
        return std::nullopt;
    }

    // Check if expired
    if (IsEntryExpired(entry)) {
        InvalidateHttpResponse(url);
        return std::nullopt;
    }

    return entry;
}

void NetworkCacheManager::InvalidateHttpResponse(const std::string& url) {
    // TODO: Implement cache invalidation
    // This requires adding a remove method to CacheManager
}

void NetworkCacheManager::ClearHttpResponses() {
    // TODO: Implement clearing all HTTP responses
}

bool NetworkCacheManager::CacheDnsEntry(const std::string& hostname,
                                       const DnsCacheEntry& entry,
                                       std::string* error) {
    // Serialize DNS entry
    std::string serialized = absl::StrJoin(entry.ip_addresses, ",");

    return cache_manager_.WritePersistentText(
        "network",
        DnsCacheKey(hostname),
        "ips",
        serialized,
        error);
}

std::optional<DnsCacheEntry> NetworkCacheManager::GetCachedDnsEntry(
    const std::string& hostname,
    std::string* error) {

    std::string serialized;
    if (!cache_manager_.ReadPersistentText(
            "network",
            DnsCacheKey(hostname),
            "ips",
            &serialized,
            error)) {
        return std::nullopt;
    }

    // Deserialize IP addresses
    DnsCacheEntry entry;
    std::vector<absl::string_view> parts = absl::StrSplit(serialized, ',');
    for (const auto& part : parts) {
        entry.ip_addresses.push_back(std::string(part));
    }
    entry.cached_at = std::chrono::system_clock::now();
    entry.ttl = default_dns_ttl_;

    // Check if expired
    if (IsEntryExpired(entry)) {
        InvalidateDnsEntry(hostname);
        return std::nullopt;
    }

    return entry;
}

void NetworkCacheManager::InvalidateDnsEntry(const std::string& hostname) {
    // TODO: Implement cache invalidation
}

void NetworkCacheManager::ClearDnsCache() {
    // TODO: Implement clearing DNS cache
}

bool NetworkCacheManager::CacheSslSession(const std::string& host,
                                         const SslSessionCacheEntry& entry,
                                         std::string* error) {
    std::vector<uint8_t> data(entry.session_data.begin(), entry.session_data.end());

    return cache_manager_.WritePersistentBinary(
        "network",
        SslCacheKey(host),
        "session",
        data,
        error);
}

std::optional<SslSessionCacheEntry> NetworkCacheManager::GetCachedSslSession(
    const std::string& host,
    std::string* error) {

    std::vector<uint8_t> data;
    if (!cache_manager_.ReadPersistentBinary(
            "network",
            SslCacheKey(host),
            "session",
            &data,
            error)) {
        return std::nullopt;
    }

    SslSessionCacheEntry entry;
    entry.session_data = std::string(data.begin(), data.end());
    entry.cached_at = std::chrono::system_clock::now();
    entry.ttl = default_ssl_ttl_;

    // Check if expired
    if (IsEntryExpired(entry)) {
        InvalidateSslSession(host);
        return std::nullopt;
    }

    return entry;
}

void NetworkCacheManager::InvalidateSslSession(const std::string& host) {
    // TODO: Implement cache invalidation
}

void NetworkCacheManager::ClearSslSessions() {
    // TODO: Implement clearing SSL sessions
}

size_t NetworkCacheManager::GetHttpResponseCount() const {
    // TODO: Implement counting
    return 0;
}

size_t NetworkCacheManager::GetDnsEntryCount() const {
    // TODO: Implement counting
    return 0;
}

size_t NetworkCacheManager::GetSslSessionCount() const {
    // TODO: Implement counting
    return 0;
}

bool NetworkCacheManager::SerializeHttpResponse(const HttpResponseCacheEntry& entry,
                                               std::vector<uint8_t>* out,
                                               std::string* error) {
    // Simple serialization: status_code (4 bytes) + headers + body
    // TODO: Implement proper serialization
    out->push_back(static_cast<uint8_t>(entry.status_code));
    out->push_back(static_cast<uint8_t>(entry.status_code >> 8));
    out->push_back(static_cast<uint8_t>(entry.status_code >> 16));
    out->push_back(static_cast<uint8_t>(entry.status_code >> 24));

    // Add body
    out->insert(out->end(), entry.body.begin(), entry.body.end());

    return true;
}

bool NetworkCacheManager::DeserializeHttpResponse(const std::vector<uint8_t>& data,
                                                 HttpResponseCacheEntry* out,
                                                 std::string* error) {
    if (data.size() < 4) {
        if (error) *error = "Invalid data size";
        return false;
    }

    // Deserialize status code
    out->status_code = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);

    // Deserialize body
    out->body.assign(data.begin() + 4, data.end());
    out->cached_at = std::chrono::system_clock::now();
    out->ttl = default_http_ttl_;

    return true;
}

} // namespace network::cache
