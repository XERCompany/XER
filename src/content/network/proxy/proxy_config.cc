/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/proxy/proxy_config.h"

#include "helper/env_utils.h"

#include <cstdlib>

#include "content/network/network_util/network_util.h"

namespace XER::Network::Proxy {

// static
ProxyConfig ProxyConfig::Direct() {
    return {};
}

// static
ProxyConfig ProxyConfig::FromEnv() {
    ProxyConfig cfg;
    std::string proxy_env = XER::Helper::GetEnv("https_proxy");
    if (proxy_env.empty()) proxy_env = XER::Helper::GetEnv("http_proxy");
    if (proxy_env.empty()) return cfg;

    std::string url = std::move(proxy_env);
    // Strip scheme
    const auto sep = url.find("://");
    ProxyType type = ProxyType::kHttp;
    if (sep != std::string::npos) {
        const std::string scheme = url.substr(0, sep);
        url = url.substr(sep + 3);
        if (scheme == "https") type = ProxyType::kHttps;
        else if (scheme == "socks4") type = ProxyType::kSocks4;
        else if (scheme == "socks5") type = ProxyType::kSocks5;
    }

    std::string host;
    uint16_t port = 0;
    XER::Network::SplitHostPort(url, &host, &port);

    cfg.proxy = {type, host, port};
    return cfg;
}

}  // namespace XER::Network::Proxy
