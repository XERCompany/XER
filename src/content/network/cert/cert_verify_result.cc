/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/cert/cert_verify_result.h"

namespace XER::network::cert {

std::string CertStatusToString(CertStatus s) {
    switch (s) {
        case CertStatus::kOk:                return "OK";
        case CertStatus::kExpired:           return "EXPIRED";
        case CertStatus::kNotYetValid:       return "NOT_YET_VALID";
        case CertStatus::kHostnameMismatch:  return "HOSTNAME_MISMATCH";
        case CertStatus::kUnknownAuthority:  return "UNKNOWN_AUTHORITY";
        case CertStatus::kRevoked:           return "REVOKED";
        case CertStatus::kInvalidSignature:  return "INVALID_SIGNATURE";
    }
    return "UNKNOWN";
}

}  // namespace XER::network::cert
