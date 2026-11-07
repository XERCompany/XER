/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "content/network/transport/transport_network_delegate.h"

namespace XER::network::transport {

struct TransportSessionParams {
    std::string host;
    uint16_t    port            = 0;
    uint32_t    connect_timeout_ms = 10000;
    uint32_t    read_timeout_ms    = 30000;
    bool        keep_alive         = true;
};

// Manages the lifecycle of a single transport-layer connection.
class TransportNetworkSession {
public:
    explicit TransportNetworkSession(TransportSessionParams params,
                                     TransportNetworkDelegate* delegate = nullptr);
    ~TransportNetworkSession();

    bool Open(std::string* error);
    void Close();
    bool IsOpen() const;

    int Read(uint8_t* buf, size_t len, std::string* error);
    int Write(const uint8_t* buf, size_t len, std::string* error);

private:
    TransportSessionParams    params_;
    TransportNetworkDelegate* delegate_ = nullptr;
    int                       fd_       = -1;
};

}  // namespace XER::network::transport
