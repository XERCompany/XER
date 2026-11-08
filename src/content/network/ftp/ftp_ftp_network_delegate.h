/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::Network::Ftp {

class FtpNetworkDelegate {
public:
    virtual ~FtpNetworkDelegate() = default;
    virtual bool OnBeforeConnect(const std::string& host, uint16_t port) { return true; }
    virtual void OnConnected(const std::string& host) {}
    virtual void OnDisconnected(const std::string& host) {}
    virtual void OnError(const std::string& error) {}
};

class DefaultFtpNetworkDelegate : public FtpNetworkDelegate {};

}  // namespace XER::Network::Ftp
