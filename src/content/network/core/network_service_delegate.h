/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>

namespace XER::network::core {

// Observer interface for top-level NetworkService events.
class NetworkServiceDelegate {
public:
    virtual ~NetworkServiceDelegate() = default;

    virtual void OnNetworkStarted() {}
    virtual void OnNetworkStopped() {}
    virtual void OnNetworkError(const std::string& error) {}
};

class DefaultNetworkServiceDelegate : public NetworkServiceDelegate {};

}  // namespace XER::network::core
