/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/cert/cert_verify_result.h"

namespace XER::network::cert {

// Verifies SSL/TLS certificate chains.
class CertVerifier {
public:
    CertVerifier() = default;
    ~CertVerifier() = default;

    // Verify a PEM-encoded certificate against |hostname|.
    bool Verify(const std::string& pem_cert,
                const std::string& hostname,
                CertVerifyResult* result) const;

    // Quick check: is the certificate within its validity window?
    static bool IsTimeValid(const CertVerifyResult& result);
};

}  // namespace XER::network::cert
