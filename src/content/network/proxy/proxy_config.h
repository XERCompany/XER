/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::network::proxy {

// Types of proxy servers supported by the network stack.
enum class ProxyType { 
    kDirect,        // No proxy (direct connection)
    kHttp,          // HTTP proxy (CONNECT tunnel for HTTPS)
    kHttps,         // HTTPS proxy
    kSocks4,        // SOCKS version 4 protocol
    kSocks5,        // SOCKS version 5 (RFC 1928, with auth)
};

// Single proxy server configuration.
struct ProxyServer {
    // Type of proxy
    ProxyType   type     = ProxyType::kDirect;
    
    // Proxy hostname or IP address
    std::string host;
    
    // Proxy port number
    uint16_t    port     = 0;
    
    // Optional username for SOCKS5 RFC 1929 authentication
    std::string username;
    
    // Optional password for SOCKS5 authentication
    std::string password;
};

struct ProxyConfig {
    bool        auto_detect   = false;
    std::string pac_url;        // PAC script URL
    ProxyServer proxy;          // manual proxy
    std::vector<std::string> bypass_list; // e.g. "localhost", "127.*"

    static ProxyConfig Direct();
    static ProxyConfig FromEnv();  // reads http_proxy / https_proxy env vars
};

}  // namespace XER::network::proxy
