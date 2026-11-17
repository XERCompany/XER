/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include <vector>

namespace XER::Network::Cert {

// Persistent store for trusted CA certificates (PEM bundles).
class CertStorage {
public:
    CertStorage(const std::string& bundle_path = "");

    // Load PEM bundle from file. Returns false on failure.
    bool Load(const std::string& path);

    // Add a single PEM certificate to the trusted set.
    void AddTrusted(const std::string& pem_cert);

    // Returns all trusted certificates.
    const std::vector<std::string>& trusted() const { return certs_; }

    // Path the bundle was loaded from ("" when certs were added
    // programmatically via AddTrusted).
    const std::string& bundle_path() const { return bundle_path_; }

    // Returns the system default CA bundle path.
    static std::string SystemCaBundlePath();

private:
    std::vector<std::string> certs_;
    std::string bundle_path_;
};

}  // namespace XER::Network::Cert
