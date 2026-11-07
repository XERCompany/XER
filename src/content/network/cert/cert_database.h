/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "content/network/cert/cert_verify_result.h"

namespace XER::network::cert {

// In-memory cache of previously verified certificate results keyed by
// (hostname + fingerprint) to avoid repeated crypto operations.
class CertDatabase {
public:
    static CertDatabase& Instance();

    void Store(const std::string& key, const CertVerifyResult& result);
    bool Lookup(const std::string& key, CertVerifyResult* result) const;
    void Remove(const std::string& key);
    void Clear();

    static std::string MakeKey(const std::string& hostname,
                               const std::string& fingerprint);

private:
    CertDatabase() = default;
    mutable std::mutex mu_;
    std::unordered_map<std::string, CertVerifyResult> cache_;
};

}  // namespace XER::network::cert
