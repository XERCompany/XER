/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/network/dns/dns_config_service.h"
#include "content/network/dns/dns_record_type.h"

namespace XER::Network::Dns {

struct DnsRecord {
    DnsRecordType type;
    std::string   name;
    std::string   value;   // IP string, CNAME target, TXT data, etc.
    uint32_t      ttl_sec = 0;
};

// Low-level UDP DNS client. Sends a single-question query to a nameserver.
class DnsClient {
public:
    explicit DnsClient(const DnsConfig& config = DnsConfigService::DefaultConfig());
    ~DnsClient() = default;

    // Query |name| for records of |type|. Returns false on error.
    bool Query(const std::string& name,
               DnsRecordType type,
               std::vector<DnsRecord>* records,
               std::string* error);

private:
    // Builds a minimal DNS query packet. Returns packet bytes.
    static std::vector<uint8_t> BuildQuery(const std::string& name,
                                           DnsRecordType type,
                                           uint16_t transaction_id);

    // Parses response packet into records.
    static bool ParseResponse(const std::vector<uint8_t>& response,
                              std::vector<DnsRecord>* records,
                              std::string* error);

    DnsConfig config_;
};

}  // namespace XER::Network::Dns
