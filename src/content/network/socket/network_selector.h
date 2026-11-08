/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>

#include "content/network/proxy/proxy_config.h"
#include "content/network/transport/transport_client_socket.h"

namespace XER::Network::Socket {

// Selects and creates the right socket type (plain/TLS/proxy) for a URL.
class NetworkSelector {
public:
    explicit NetworkSelector(const Proxy::ProxyConfig& proxy_config = {});

    std::unique_ptr<Transport::TransportClientSocket>
    CreateSocket(const std::string& scheme,
                 const std::string& host,
                 uint16_t port,
                 std::string* error);

private:
    Proxy::ProxyConfig proxy_config_;
};

}  // namespace XER::Network::Socket
