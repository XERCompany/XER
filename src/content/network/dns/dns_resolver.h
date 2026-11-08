/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "content/network/dns/dns_config_service.h"
#include "content/network/dns/dns_record_type.h"
#include "content/network/dns/host_resolver.h"

namespace XER::Network::Dns {

// High-level resolver: tries cache → system getaddrinfo fallback.
class DnsResolver {
public:
    explicit DnsResolver(
        const DnsConfig& config = DnsConfigService::DefaultConfig());
    ~DnsResolver() = default;

    // Resolve hostname to addresses (blocks).
    bool Resolve(const std::string& host,
                 uint16_t port,
                 std::vector<ResolvedAddress>* out,
                 std::string* error);

    // Async variant.
    void ResolveAsync(const std::string& host,
                      uint16_t port,
                      ResolveCallback callback);

    void ClearCache();

private:
    DnsConfig                         config_;
    std::unique_ptr<HostResolver>     host_resolver_;
};

}  // namespace XER::Network::Dns
