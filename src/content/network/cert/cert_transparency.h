/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Network::Cert {

struct SignedCertTimestamp {
    std::string log_id;       // base64 log ID
    uint64_t    timestamp_ms = 0;
    std::string signature;    // base64 signature
};

// Certificate Transparency (RFC 6962) utilities.
class CertTransparency {
public:
    // Parse SCTs from TLS extension bytes. Returns false if none found.
    static bool ParseSCTs(const std::vector<uint8_t>& extension_data,
                          std::vector<SignedCertTimestamp>* scts);

    // Returns true if the |scts| list meets policy (at least |min_scts|).
    static bool PolicySatisfied(const std::vector<SignedCertTimestamp>& scts,
                                int min_scts = 2);
};

}  // namespace XER::Network::Cert
