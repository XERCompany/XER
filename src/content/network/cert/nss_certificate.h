/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Network::Cert {

// Wraps a DER-encoded certificate with convenience helpers.
class NssCertificate {
public:
    NssCertificate() = default;
    explicit NssCertificate(std::vector<uint8_t> der);

    bool IsEmpty() const { return der_.empty(); }

    const std::vector<uint8_t>& der() const { return der_; }

    // Convert to PEM string.
    std::string ToPEM() const;

    // Load from PEM string.
    static NssCertificate FromPEM(const std::string& pem);

private:
    std::vector<uint8_t> der_;
};

}  // namespace XER::Network::Cert
