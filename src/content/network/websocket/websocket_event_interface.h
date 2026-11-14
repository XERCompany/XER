/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace XER::Network::WebSocket {

// Observer interface for WebSocket lifecycle events.
class WebSocketEventInterface {
public:
    virtual ~WebSocketEventInterface() = default;

    virtual void OnAddChannelResponse(bool,
                                      const std::string&,
                                      const std::string&) {}
    virtual void OnDataFrame(bool,
                              bool,
                              const std::vector<uint8_t>&) {}
    virtual void OnFlowControl(int64_t) {}
    virtual void OnClosingHandshake() {}
    virtual void OnDropChannel(bool,
                                uint16_t,
                                const std::string&) {}
    virtual void OnFailChannel(const std::string&) {}
};

}  // namespace XER::Network::WebSocket
