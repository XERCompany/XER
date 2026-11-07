/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::network::dns {

struct DnsConfig {
    std::vector<std::string> nameservers;  // e.g. {"8.8.8.8", "8.8.4.4"}
    std::vector<std::string> search;       // search domains
    uint32_t timeout_ms  = 5000;
    int      ndots       = 1;
    bool     rotate      = false;
    bool     use_tcp     = false;
};

// Reads system DNS configuration (/etc/resolv.conf on Linux).
class DnsConfigService {
public:
    DnsConfigService() = default;
    ~DnsConfigService() = default;

    // Reads and returns the current system DNS config.
    // Returns false if the config cannot be read.
    bool ReadConfig(DnsConfig* config) const;

    // Returns a default config suitable for fallback.
    static DnsConfig DefaultConfig();
};

}  // namespace XER::network::dns
