/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/proxy/proxy_resolver_factory.h"
#include "content/network/proxy/proxy_service.h"

namespace XER::network::proxy {

// static
std::unique_ptr<ProxyResolver> ProxyResolverFactory::Create(
    const ProxyConfig& config) {
    return std::make_unique<ProxyService>(config);
}

}  // namespace XER::network::proxy
