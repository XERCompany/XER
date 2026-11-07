/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "content/network/transport/transport_client_socket.h"

namespace XER::network::transport {

// Abstract server socket interface.
class TransportServerSocket {
public:
    virtual ~TransportServerSocket() = default;

    virtual bool Listen(const std::string& address,
                        uint16_t port,
                        int backlog,
                        std::string* error) = 0;
    virtual std::unique_ptr<TransportClientSocket> Accept(std::string* error) = 0;
    virtual void Close() = 0;
    virtual bool IsListening() const = 0;
};

}  // namespace XER::network::transport
