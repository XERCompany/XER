/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/dns/host_resolver_impl.h"

#include "content/network/async/async_operation_manager.h"
#include "content/network/cache/network_cache_manager.h"
#include "content/network/logging/network_logger.h"
#include "content/network/socket/platform/socket_platform.h"

#include <chrono>

namespace platform = network::socket::platform;

namespace network::dns {

namespace {
uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
}  // namespace

HostResolverImpl::HostResolverImpl(const DnsConfig& config) : config_(config) {
    async::AsyncOperationManager::Instance().Start();
}

bool HostResolverImpl::Resolve(const std::string& host,
                                uint16_t /*port*/,
                                std::vector<ResolvedAddress>* out,
                                std::string* error) {
    auto start_time = NowMs();
    logging::NetworkLogger::Instance().LogDnsQuery(host);

    // Check cache first
    if (LookupCache(host, out)) {
        auto duration = NowMs() - start_time;
        std::vector<std::string> ips;
        for (const auto& addr : *out) {
            ips.push_back(addr.address);
        }
        logging::NetworkLogger::Instance().LogDnsResponse(host, ips, true, duration);
        return true;
    }

    // Use platform abstraction for DNS resolution
    auto& platform = platform::SocketPlatform::GetInstance();
    std::vector<platform::AddressInfo> addresses;

    if (!platform.ResolveAddress(host, 0, &addresses, error)) {
        logging::NetworkLogger::Instance().LogDnsError(host, *error);
        return false;
    }

    // Convert to ResolvedAddress format
    std::vector<ResolvedAddress> addrs;
    for (const auto& addr : addresses) {
        addrs.push_back({addr.ip_address, 60});  // Default TTL 60 seconds
    }

    // Store in cache
    StoreCache(host, addrs);

    *out = std::move(addrs);

    auto duration = NowMs() - start_time;
    std::vector<std::string> ips;
    for (const auto& addr : *out) {
        ips.push_back(addr.address);
    }
    logging::NetworkLogger::Instance().LogDnsResponse(host, ips, false, duration);

    return true;
}

void HostResolverImpl::ResolveAsync(const std::string& host,
                                    uint16_t port,
                                    ResolveCallback callback) {
    async::AsyncOperationManager::Instance().Enqueue(
        [this, host, port, cb = std::move(callback)]() mutable {
            std::vector<ResolvedAddress> addrs;
            std::string error;
            const bool ok = Resolve(host, port, &addrs, &error);
            cb(ok, addrs, error);
        });
}

bool HostResolverImpl::LookupCache(const std::string& host,
                                    std::vector<ResolvedAddress>* out) const {
    // Try new NetworkCacheManager first
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    std::string error;
    auto cached_entry = cache_mgr.GetCachedDnsEntry(host, &error);

    if (cached_entry) {
        // Convert to ResolvedAddress format
        for (const auto& ip : cached_entry->ip_addresses) {
            out->push_back({ip, static_cast<uint32_t>(cached_entry->ttl.count())});
        }
        logging::NetworkLogger::Instance().LogCacheHit("dns", host);
        return true;
    }

    logging::NetworkLogger::Instance().LogCacheMiss("dns", host);

    // Fallback to in-memory cache
    std::lock_guard<std::mutex> lock(cache_mu_);
    auto it = cache_.find(host);
    if (it == cache_.end()) return false;
    if (NowMs() > it->second.expires_at_ms) return false;
    *out = it->second.addrs;
    return true;
}

void HostResolverImpl::StoreCache(const std::string& host,
                                   const std::vector<ResolvedAddress>& addrs) {
    if (addrs.empty()) return;

    const uint32_t ttl = addrs[0].ttl_sec ? addrs[0].ttl_sec : 60;

    // Store in NetworkCacheManager
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    cache::DnsCacheEntry entry;
    for (const auto& addr : addrs) {
        entry.ip_addresses.push_back(addr.address);
    }
    entry.ttl = std::chrono::seconds(ttl);

    std::string error;
    if (cache_mgr.CacheDnsEntry(host, entry, &error)) {
        logging::NetworkLogger::Instance().LogCacheStore("dns", host, entry.ip_addresses.size());
    }

    // Also store in in-memory cache as backup
    std::lock_guard<std::mutex> lock(cache_mu_);
    cache_[host] = {addrs, NowMs() + static_cast<uint64_t>(ttl) * 1000ULL};
}

void HostResolverImpl::ClearCache() {
    // Clear in-memory cache
    std::lock_guard<std::mutex> lock(cache_mu_);
    cache_.clear();

    // Clear NetworkCacheManager DNS cache
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    cache_mgr.ClearDnsCache();
}

}  // namespace network::dns
