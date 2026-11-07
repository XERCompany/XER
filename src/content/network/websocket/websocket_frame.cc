/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/websocket/websocket_frame.h"

#include <cstring>
#include <random>

namespace XER::network::websocket {

static uint32_t RandomMask() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng();
}

std::vector<uint8_t> EncodeFrame(const WebSocketFrame& frame) {
    std::vector<uint8_t> out;
    const uint8_t b0 = (frame.fin ? 0x80 : 0) |
                       static_cast<uint8_t>(frame.opcode);
    out.push_back(b0);

    const size_t plen = frame.payload.size();
    const uint8_t mask_bit = frame.masked ? 0x80 : 0x00;
    if (plen < 126) {
        out.push_back(mask_bit | static_cast<uint8_t>(plen));
    } else if (plen < 65536) {
        out.push_back(mask_bit | 126);
        out.push_back(static_cast<uint8_t>(plen >> 8));
        out.push_back(static_cast<uint8_t>(plen));
    } else {
        out.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>(plen >> (i * 8)));
    }

    if (frame.masked) {
        const uint32_t mk = RandomMask();
        uint8_t mkey[4];
        std::memcpy(mkey, &mk, 4);
        for (int i = 0; i < 4; ++i) out.push_back(mkey[i]);
        for (size_t i = 0; i < plen; ++i)
            out.push_back(frame.payload[i] ^ mkey[i % 4]);
    } else {
        out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    }
    return out;
}

size_t DecodeFrame(const uint8_t* data, size_t len, WebSocketFrame* frame) {
    if (len < 2) return 0;
    frame->fin    = (data[0] & 0x80) != 0;
    frame->opcode = static_cast<WebSocketOpcode>(data[0] & 0x0F);
    const bool masked = (data[1] & 0x80) != 0;
    frame->masked = masked;
    uint64_t plen = data[1] & 0x7F;
    size_t hdr = 2;
    if (plen == 126) {
        if (len < 4) return 0;
        plen = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        hdr = 4;
    } else if (plen == 127) {
        if (len < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; ++i) plen = (plen << 8) | data[2+i];
        hdr = 10;
    }
    const size_t total = hdr + (masked ? 4 : 0) + static_cast<size_t>(plen);
    if (len < total) return 0;
    if (masked) {
        const uint8_t* mkey = data + hdr;
        const uint8_t* payload = mkey + 4;
        frame->payload.resize(static_cast<size_t>(plen));
        for (size_t i = 0; i < static_cast<size_t>(plen); ++i)
            frame->payload[i] = payload[i] ^ mkey[i % 4];
    } else {
        frame->payload.assign(data + hdr, data + hdr + static_cast<size_t>(plen));
    }
    return total;
}

WebSocketFrame MakeCloseFrame(uint16_t code, const std::string& reason) {
    WebSocketFrame f;
    f.opcode = WebSocketOpcode::kClose;
    f.payload.push_back(static_cast<uint8_t>(code >> 8));
    f.payload.push_back(static_cast<uint8_t>(code));
    for (char c : reason) f.payload.push_back(static_cast<uint8_t>(c));
    return f;
}

}  // namespace XER::network::websocket
