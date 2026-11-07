/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/proxy/proxy_config.h"

namespace XER::network::proxy {

// Result of proxy resolution for a specific URL.
struct ProxyInfo {
    bool        is_direct = true;
    ProxyServer server;

    static ProxyInfo Direct() { return {}; }
    static ProxyInfo Via(const ProxyServer& s) { return {false, s}; }
};

}  // namespace XER::network::proxy
