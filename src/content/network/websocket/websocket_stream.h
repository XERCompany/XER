/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include "content/network/websocket/websocket_frame.h"
#include "content/network/transport/transport_client_socket.h"

namespace XER::Network::WebSocket {

using MessageCallback = std::function<void(const WebSocketFrame& frame)>;
using CloseCallback   = std::function<void(uint16_t code, const std::string& reason)>;
using ErrorCallback   = std::function<void(const std::string& error)>;
using WebSocketCallback = std::function<void(bool, const std::string&)>;

// Full-duplex WebSocket stream (post-handshake).
class WebSocketStream {
public:
    explicit WebSocketStream(
        std::unique_ptr<Transport::TransportClientSocket> socket);
    ~WebSocketStream();

    void SetOnMessage(MessageCallback cb) { on_message_ = std::move(cb); }
    void SetOnClose(CloseCallback cb)     { on_close_   = std::move(cb); }
    void SetOnError(ErrorCallback cb)     { on_error_   = std::move(cb); }

    // Synchronous operations
    bool SendText(const std::string& text, std::string* error = nullptr);
    bool SendBinary(const uint8_t* data, size_t len, std::string* error = nullptr);
    bool SendPing(std::string* error = nullptr);
    void Close(uint16_t code = 1000, const std::string& reason = {});

    // Asynchronous operations
    void SendTextAsync(const std::string& text, WebSocketCallback cb);
    void SendBinaryAsync(const uint8_t* data, size_t len, WebSocketCallback cb);

    // Read loop (blocking - call from a thread)
    void ReadLoop();

    // Read loop (async - runs in thread pool)
    void ReadLoopAsync();

    bool IsOpen() const { return open_; }

private:
    std::unique_ptr<Transport::TransportClientSocket> sock_;
    MessageCallback on_message_;
    CloseCallback   on_close_;
    ErrorCallback   on_error_;
    bool            open_ = false;
};

}  // namespace XER::Network::WebSocket
