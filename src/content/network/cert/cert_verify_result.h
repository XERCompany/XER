/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace XER::network::cert {

enum class CertStatus {
    kOk = 0,
    kExpired,
    kNotYetValid,
    kHostnameMismatch,
    kUnknownAuthority,
    kRevoked,
    kInvalidSignature,
};

struct CertVerifyResult {
    bool                    is_valid     = false;
    CertStatus              status       = CertStatus::kOk;
    std::string             subject;
    std::string             issuer;
    std::time_t             not_before   = 0;
    std::time_t             not_after    = 0;
    std::vector<std::string> san;       // Subject Alternative Names
    std::string             fingerprint; // SHA-256 hex
    std::string             error_message;
};

std::string CertStatusToString(CertStatus s);

}  // namespace XER::network::cert
