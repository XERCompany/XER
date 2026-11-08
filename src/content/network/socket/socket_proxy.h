/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>

#include "content/network/proxy/proxy_config.h"
#include "content/network/socket/tcp_client_socket.h"

namespace XER::Network::Socket {

// Opens a TCP socket through a proxy (HTTP CONNECT or SOCKS).
class SocketProxy {
public:
    // Creates a connected socket through the given proxy to target host:port.
    static std::unique_ptr<TcpClientSocket>
    ConnectThrough(const Proxy::ProxyServer& proxy,
                   const std::string& target_host,
                   uint16_t target_port,
                   std::string* error);
};

}  // namespace XER::Network::Socket
