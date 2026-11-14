/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::Network::Transport {

// Delegate for observing raw transport events.
class TransportNetworkDelegate {
public:
    virtual ~TransportNetworkDelegate() = default;

    virtual void OnConnected(const std::string&, uint16_t) {}
    virtual void OnDisconnected(const std::string&, uint16_t) {}
    virtual void OnBytesSent(size_t) {}
    virtual void OnBytesReceived(size_t) {}
    virtual void OnError(const std::string&) {}
};

// No-op default delegate.
class DefaultTransportDelegate : public TransportNetworkDelegate {};

}  // namespace XER::Network::Transport
