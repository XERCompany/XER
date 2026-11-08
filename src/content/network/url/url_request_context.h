/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>

#include "content/network/proxy/proxy_config.h"
#include "content/network/cert/cert_storage.h"
#include "content/network/dns/dns_config_service.h"

namespace XER::Network::Url {

struct URLRequestContext {
    std::string              user_agent   = "XER/1.0";
    Proxy::ProxyConfig       proxy_config;
    Dns::DnsConfig           dns_config;
    uint32_t                 socket_timeout_ms = 30000;
    bool                     skip_ssl_verify   = false;
    // Note: CertStorage will be default-initialized
    Cert::CertStorage        cert_storage;
};

}  // namespace XER::Network::Url
