/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::Network::WebSocket {

class WebSocketNetworkDelegate {
public:
    virtual ~WebSocketNetworkDelegate() = default;

    virtual bool OnBeforeConnect(const std::string&) { return true; }
    virtual void OnConnected(const std::string&) {}
    virtual void OnDisconnected(const std::string&,
                                 uint16_t,
                                 const std::string&) {}
    virtual void OnError(const std::string&,
                          const std::string&) {}
};

class DefaultWebSocketNetworkDelegate : public WebSocketNetworkDelegate {};

}  // namespace XER::Network::WebSocket
