/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "content/network/dns/dns_config_service.h"
#include "content/network/dns/host_resolver.h"
#include "async_io/io_thread_pool.h"

namespace network::dns {

// Default HostResolver implementation using getaddrinfo(3).
class HostResolverImpl : public HostResolver {
public:
    explicit HostResolverImpl(const DnsConfig& config = DnsConfigService::DefaultConfig());
    ~HostResolverImpl() override = default;

    bool Resolve(const std::string& host,
                 uint16_t port,
                 std::vector<ResolvedAddress>* out,
                 std::string* error) override;

    void ResolveAsync(const std::string& host,
                      uint16_t port,
                      ResolveCallback callback) override;

    // Clear in-memory TTL cache.
    void ClearCache();

private:
    struct CacheEntry {
        std::vector<ResolvedAddress> addrs;
        uint64_t expires_at_ms;
    };

    bool LookupCache(const std::string& host, std::vector<ResolvedAddress>* out) const;
    void StoreCache(const std::string& host, const std::vector<ResolvedAddress>& addrs);

    DnsConfig config_;
    mutable std::mutex cache_mu_;
    std::unordered_map<std::string, CacheEntry> cache_;
    AsyncIO::IO::Async::IOThreadPool pool_{2};
};

}  // namespace network::dns
