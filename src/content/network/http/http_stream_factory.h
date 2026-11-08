/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>
#include "content/network/http/http_stream.h"
#include "content/network/transport/transport_client_socket.h"

namespace XER::Network::Http {

// Creates an HttpStream from a connected TransportClientSocket.
class HttpStreamFactory {
public:
    static std::unique_ptr<HttpStream>
    CreateFromSocket(std::unique_ptr<Transport::TransportClientSocket> socket,
                     const std::string& host,
                     std::string* error);
};

}  // namespace XER::Network::Http
