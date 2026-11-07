/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/spdy/spdy_framer.h"

namespace XER::network::spdy {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

// Read a 4-byte big-endian uint32, masking out the MSB (stream IDs).
inline uint32_t ReadU32StreamId(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0] & 0x7F) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
}

// Read a plain 4-byte big-endian uint32 (no MSB mask).
inline uint32_t ReadU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
}

// Control frame common header:
// [1bit=1][15bit version][16bit type][8bit flags][24bit length]
// total: 8 bytes
size_t ParseControlFrame(const uint8_t* buf, size_t len, SpdyFrame* out) {
    // Already know buf[0] & 0x80 != 0 (caller checked).
    // Version: buf[0..1] & 0x7FFF
    // const uint16_t version = ((buf[0] & 0x7F) << 8) | buf[1];  // unused but could validate
    const uint16_t frame_type_raw = (static_cast<uint16_t>(buf[2]) << 8) | buf[3];
    const uint8_t  flags = buf[4];
    const uint32_t payload_len = (static_cast<uint32_t>(buf[5]) << 16) |
                                 (static_cast<uint32_t>(buf[6]) << 8) |
                                  static_cast<uint32_t>(buf[7]);

    if (len < 8 + payload_len) {
        return 0;  // Incomplete
    }

    const uint8_t* payload = buf + 8;
    out->control = true;
    out->fin     = (flags & 0x01) != 0;

    switch (frame_type_raw) {
        // ── SYN_STREAM (type=1) ─────────────────────────────────────────────
        // [stream_id:4][assoc_stream_id:4][priority:2bit+unused:6bit][unused:8bit]
        // [compressed headers...]
        case 1: {
            out->type = SpdyFrameType::kSynStream;
            if (payload_len < 10) break;
            out->stream_id = ReadU32StreamId(payload);
            // Associated stream ID at payload+4 (MSB masked) — store in data[0..3]
            // Header payload starts at payload+10; store raw in data.
            if (payload_len > 10) {
                out->data.assign(payload + 10, payload + payload_len);
            }
            break;
        }
        // ── SYN_REPLY (type=2) ──────────────────────────────────────────────
        // [stream_id:4][unused:2][compressed headers...]
        case 2: {
            out->type = SpdyFrameType::kSynReply;
            if (payload_len < 6) break;
            out->stream_id = ReadU32StreamId(payload);
            if (payload_len > 6) {
                out->data.assign(payload + 6, payload + payload_len);
            }
            break;
        }
        // ── RST_STREAM (type=3) ─────────────────────────────────────────────
        // [stream_id:4][status_code:4]  — fixed 8 bytes
        case 3: {
            out->type = SpdyFrameType::kRstStream;
            if (payload_len < 8) break;
            out->stream_id = ReadU32StreamId(payload);
            // Encode status code in data[0..3] as big-endian.
            const uint32_t status = ReadU32(payload + 4);
            out->data.resize(4);
            out->data[0] = static_cast<uint8_t>(status >> 24);
            out->data[1] = static_cast<uint8_t>(status >> 16);
            out->data[2] = static_cast<uint8_t>(status >> 8);
            out->data[3] = static_cast<uint8_t>(status);
            break;
        }
        // ── SETTINGS (type=4) ───────────────────────────────────────────────
        // [num_entries:4]  then for each: [id:3 LE][flags:1][value:4 BE]
        case 4: {
            out->type = SpdyFrameType::kSettings;
            // Store the raw settings payload; callers can decode as needed.
            if (payload_len > 0) {
                out->data.assign(payload, payload + payload_len);
            }
            break;
        }
        // ── PING (type=6) ───────────────────────────────────────────────────
        // [unique_id:4]
        case 6: {
            out->type = SpdyFrameType::kPing;
            if (payload_len < 4) break;
            // Store ping ID in stream_id for convenience.
            out->stream_id = ReadU32(payload);
            break;
        }
        // ── GOAWAY (type=7) ─────────────────────────────────────────────────
        // [last_good_stream_id:4][status_code:4]
        case 7: {
            out->type = SpdyFrameType::kGoAway;
            if (payload_len < 8) break;
            out->stream_id = ReadU32StreamId(payload);  // last good stream id
            const uint32_t status = ReadU32(payload + 4);
            out->data.resize(4);
            out->data[0] = static_cast<uint8_t>(status >> 24);
            out->data[1] = static_cast<uint8_t>(status >> 16);
            out->data[2] = static_cast<uint8_t>(status >> 8);
            out->data[3] = static_cast<uint8_t>(status);
            break;
        }
        // ── HEADERS (type=8) ────────────────────────────────────────────────
        // [stream_id:4][unused:2][compressed headers...]
        case 8: {
            out->type = SpdyFrameType::kHeaders;
            if (payload_len < 6) break;
            out->stream_id = ReadU32StreamId(payload);
            if (payload_len > 6) {
                out->data.assign(payload + 6, payload + payload_len);
            }
            break;
        }
        // ── WINDOW_UPDATE (type=9) ──────────────────────────────────────────
        // [stream_id:4][delta_window_size:4]
        case 9: {
            out->type = SpdyFrameType::kWindowUpdate;
            if (payload_len < 8) break;
            out->stream_id = ReadU32StreamId(payload);
            const uint32_t delta = ReadU32(payload + 4) & 0x7FFFFFFF;
            out->data.resize(4);
            out->data[0] = static_cast<uint8_t>(delta >> 24);
            out->data[1] = static_cast<uint8_t>(delta >> 16);
            out->data[2] = static_cast<uint8_t>(delta >> 8);
            out->data[3] = static_cast<uint8_t>(delta);
            break;
        }
        default:
            // Unknown control frame type — store raw payload, preserve type bits.
            out->type = SpdyFrameType::kData;
            if (payload_len > 0) {
                out->data.assign(payload, payload + payload_len);
            }
            break;
    }

    return 8 + payload_len;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Minimal SPDY/3 data frame serialization: [stream_id(4)][flags(1)][length(3)][payload]
std::vector<uint8_t> SerializeSpdyFrame(const SpdyFrame& frame) {
    std::vector<uint8_t> out;
    if (!frame.control) {
        // Data frame: first bit 0
        const uint32_t sid = frame.stream_id & 0x7FFFFFFF;
        out.push_back(static_cast<uint8_t>(sid >> 24));
        out.push_back(static_cast<uint8_t>(sid >> 16));
        out.push_back(static_cast<uint8_t>(sid >> 8));
        out.push_back(static_cast<uint8_t>(sid));
        out.push_back(frame.fin ? 0x01 : 0x00);
        const uint32_t len = static_cast<uint32_t>(frame.data.size());
        out.push_back(static_cast<uint8_t>(len >> 16));
        out.push_back(static_cast<uint8_t>(len >> 8));
        out.push_back(static_cast<uint8_t>(len));
        out.insert(out.end(), frame.data.begin(), frame.data.end());
    }
    return out;
}

size_t ParseSpdyFrame(const uint8_t* buf, size_t len, SpdyFrame* out) {
    if (len < 8 || out == nullptr) return 0;
    const bool ctrl = (buf[0] & 0x80) != 0;
    out->control = ctrl;
    if (!ctrl) {
        // Data frame
        out->type      = SpdyFrameType::kData;
        out->stream_id = ReadU32StreamId(buf);
        out->fin       = (buf[4] & 0x01) != 0;
        const uint32_t plen = (static_cast<uint32_t>(buf[5]) << 16) |
                              (static_cast<uint32_t>(buf[6]) << 8) |
                               static_cast<uint32_t>(buf[7]);
        if (len < 8 + plen) return 0;
        out->data.assign(buf + 8, buf + 8 + plen);
        return 8 + plen;
    }
    return ParseControlFrame(buf, len, out);
}

}  // namespace XER::network::spdy
