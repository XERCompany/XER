/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::network::dns {

// Standard DNS record types (RFC 1035 + extensions).
enum class DnsRecordType : uint16_t {
    kA     = 1,    // IPv4 address
    kNS    = 2,    // Name server
    kCNAME = 5,    // Canonical name
    kSOA   = 6,    // Start of authority
    kPTR   = 12,   // Pointer (reverse DNS)
    kMX    = 15,   // Mail exchange
    kTXT   = 16,   // Text
    kAAAA  = 28,   // IPv6 address
    kSRV   = 33,   // Service locator
    kHTTPS = 65,   // HTTPS SVCB record
    kAny   = 255,  // Wildcard
};

std::string DnsRecordTypeToString(DnsRecordType type);
bool DnsRecordTypeFromString(const std::string& name, DnsRecordType* out);

}  // namespace XER::network::dns
