/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>
#include "content/network/transport/transport_client_socket.h"

namespace XER::network::websocket {

// Performs the WebSocket HTTP Upgrade handshake (RFC 6455 §4).
class WebSocketHandshakeStream {
public:
    // Sends the upgrade request and validates the server response.
    // Returns true on success; |error| set otherwise.
    static bool DoHandshake(transport::TransportClientSocket* sock,
                             const std::string& host,
                             const std::string& path,
                             const std::string& origin,
                             std::string* error);

private:
    static std::string GenerateSecKey();
    static std::string ExpectedAcceptKey(const std::string& sec_key);
};

}  // namespace XER::network::websocket
