/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include "content/network/proxy/proxy_config.h"
#include "content/network/proxy/proxy_resolver.h"

namespace XER::Network::Proxy {

// Concrete resolver that uses a static ProxyConfig.
class ProxyService : public ProxyResolver {
public:
    explicit ProxyService(const ProxyConfig& config);

    bool Resolve(const std::string& url,
                 ProxyInfo* info,
                 std::string* error) override;

private:
    bool IsBypassed(const std::string& host) const;

    ProxyConfig config_;
};

}  // namespace XER::Network::Proxy
