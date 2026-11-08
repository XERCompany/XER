/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <mutex>
#include <string>

namespace XER::Network::Http {

// Records server-advertised properties (HSTS, ALPN, alt-svc).
class HttpServerProperties {
public:
    static HttpServerProperties& Instance();

    void   SetSupportsSPDY(const std::string& host, bool supported);
    bool   SupportsSPDY(const std::string& host) const;

    void   SetRequiresHTTPS(const std::string& host, bool required);
    bool   RequiresHTTPS(const std::string& host) const;

    void   Clear();

private:
    HttpServerProperties() = default;
    mutable std::mutex mu_;
    std::map<std::string, bool> spdy_support_;
    std::map<std::string, bool> https_required_;
};

}  // namespace XER::Network::Http
