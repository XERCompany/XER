/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace XER::network::websocket {

// Observer interface for WebSocket lifecycle events.
class WebSocketEventInterface {
public:
    virtual ~WebSocketEventInterface() = default;

    virtual void OnAddChannelResponse(bool success,
                                      const std::string& selected_protocol,
                                      const std::string& extensions) {}
    virtual void OnDataFrame(bool fin,
                              bool is_text,
                              const std::vector<uint8_t>& data) {}
    virtual void OnFlowControl(int64_t quota) {}
    virtual void OnClosingHandshake() {}
    virtual void OnDropChannel(bool was_clean,
                                uint16_t code,
                                const std::string& reason) {}
    virtual void OnFailChannel(const std::string& message) {}
};

}  // namespace XER::network::websocket
