/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/cert/cert_database.h"

#include <mutex>

namespace XER::Network::Cert {

// static
CertDatabase& CertDatabase::Instance() {
    static CertDatabase instance;
    return instance;
}

void CertDatabase::Store(const std::string& key, const CertVerifyResult& result) {
    std::lock_guard<std::mutex> lock(mu_);
    cache_[key] = result;
}

bool CertDatabase::Lookup(const std::string& key, CertVerifyResult* result) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cache_.find(key);
    if (it == cache_.end()) return false;
    *result = it->second;
    return true;
}

void CertDatabase::Remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    cache_.erase(key);
}

void CertDatabase::Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    cache_.clear();
}

// static
std::string CertDatabase::MakeKey(const std::string& hostname,
                                  const std::string& fingerprint) {
    return hostname + ':' + fingerprint;
}

}  // namespace XER::Network::Cert
