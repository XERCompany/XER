/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/cert/cert_storage.h"

#include <fstream>
#include <sstream>

namespace XER::Network::Cert {

CertStorage::CertStorage(const std::string& bundle_path) {
    if (!bundle_path.empty()) Load(bundle_path);
}

bool CertStorage::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::ostringstream current;
    std::string line;
    bool in_cert = false;

    while (std::getline(file, line)) {
        if (line.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
            current.str("");
            current.clear();
            in_cert = true;
        }
        if (in_cert) current << line << '\n';
        if (in_cert && line.find("-----END CERTIFICATE-----") != std::string::npos) {
            certs_.push_back(current.str());
            in_cert = false;
        }
    }
    return true;
}

void CertStorage::AddTrusted(const std::string& pem_cert) {
    certs_.push_back(pem_cert);
}

// static
std::string CertStorage::SystemCaBundlePath() {
    // Common Linux locations
    constexpr const char* kPaths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/usr/share/ca-certificates/ca-certificates.crt",
    };
    for (const auto* p : kPaths) {
        if (std::ifstream(p).good()) return p;
    }
    return {};
}

}  // namespace XER::Network::Cert
