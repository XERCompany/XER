/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/proxy/proxy_info.h"

namespace XER::Network::Proxy {

// Abstract proxy resolver: maps a URL → ProxyInfo.
class ProxyResolver {
public:
    virtual ~ProxyResolver() = default;
    virtual bool Resolve(const std::string& url,
                         ProxyInfo* info,
                         std::string* error) = 0;
};

}  // namespace XER::Network::Proxy
