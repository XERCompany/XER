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

    virtual bool OnBeforeConnect(const std::string& url) { return true; }
    virtual void OnConnected(const std::string& url) {}
    virtual void OnDisconnected(const std::string& url,
                                 uint16_t code,
                                 const std::string& reason) {}
    virtual void OnError(const std::string& url,
                          const std::string& error) {}
};

class DefaultWebSocketNetworkDelegate : public WebSocketNetworkDelegate {};

}  // namespace XER::Network::WebSocket
