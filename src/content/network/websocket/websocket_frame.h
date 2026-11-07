/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::network::websocket {

// WebSocket frame opcodes as defined in RFC 6455 Section 5.2.
enum class WebSocketOpcode : uint8_t {
    kContinuation = 0x0,   // Continuation frame (for fragmented messages)
    kText         = 0x1,   // Text frame (UTF-8 encoded message)
    kBinary       = 0x2,   // Binary frame (arbitrary binary data)
    kClose        = 0x8,   // Connection close frame
    kPing         = 0x9,   // Ping frame (keep-alive/latency check)
    kPong         = 0xA,   // Pong frame (response to ping)
};

// WebSocket protocol frame as per RFC 6455.
// Each frame has:
//   - FIN bit: indicates if this is the final fragment
//   - Opcode: frame type (text, binary, control, etc.)
//   - Masking: client frames MUST be masked, server frames MUST NOT be
//   - Payload: message data (0-2^63 bytes)
struct WebSocketFrame {
    // True if this is the final fragment of the message
    bool            fin     = true;
    
    // True if payload is XOR-masked (required for client->server)
    bool            masked  = false;
    
    // Frame type/opcode
    WebSocketOpcode opcode  = WebSocketOpcode::kText;
    
    // Raw payload bytes (up to 2^63 bytes)
    std::vector<uint8_t> payload;
};

// Encode a frame into wire bytes.
std::vector<uint8_t> EncodeFrame(const WebSocketFrame& frame);

// Decode one frame from |data|. Returns bytes consumed, 0 if incomplete.
size_t DecodeFrame(const uint8_t* data, size_t len, WebSocketFrame* frame);

// Build a Close frame with optional status code and reason.
WebSocketFrame MakeCloseFrame(uint16_t code = 1000,
                               const std::string& reason = {});

}  // namespace XER::network::websocket
