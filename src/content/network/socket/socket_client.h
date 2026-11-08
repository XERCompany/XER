/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/socket/ssl_client_socket.h"

namespace XER::Network::Socket {
// Factory: creates a TcpClientSocket or SslClientSocket based on scheme.
std::unique_ptr<Transport::TransportClientSocket>
CreateSocketForScheme(const std::string& scheme);
}  // namespace XER::Network::Socket
