/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>

#include "content/network/proxy/proxy_config.h"
#include "content/network/transport/transport_client_socket.h"

namespace XER::network::socket {

// Selects and creates the right socket type (plain/TLS/proxy) for a URL.
class NetworkSelector {
public:
    explicit NetworkSelector(const proxy::ProxyConfig& proxy_config = {});

    std::unique_ptr<transport::TransportClientSocket>
    CreateSocket(const std::string& scheme,
                 const std::string& host,
                 uint16_t port,
                 std::string* error);

private:
    proxy::ProxyConfig proxy_config_;
};

}  // namespace XER::network::socket
