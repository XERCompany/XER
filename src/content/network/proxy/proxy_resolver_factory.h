/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include "content/network/proxy/proxy_config.h"
#include "content/network/proxy/proxy_resolver.h"

namespace XER::network::proxy {

// Creates a ProxyResolver appropriate for the given config.
class ProxyResolverFactory {
public:
    static std::unique_ptr<ProxyResolver> Create(const ProxyConfig& config);
};

}  // namespace XER::network::proxy
