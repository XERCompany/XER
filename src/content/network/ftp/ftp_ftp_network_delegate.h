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
    virtual bool OnBeforeConnect(const std::string&, uint16_t) { return true; }
    virtual void OnConnected(const std::string&) {}
    virtual void OnDisconnected(const std::string&) {}
    virtual void OnError(const std::string&) {}
};

class DefaultFtpNetworkDelegate : public FtpNetworkDelegate {};

}  // namespace XER::Network::Ftp
