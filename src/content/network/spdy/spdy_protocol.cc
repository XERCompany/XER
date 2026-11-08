/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/spdy/spdy_protocol.h"

namespace XER::Network::Spdy {

std::string SpdyFrameTypeToString(SpdyFrameType t) {
    switch (t) {
        case SpdyFrameType::kData:         return "DATA";
        case SpdyFrameType::kSynStream:    return "SYN_STREAM";
        case SpdyFrameType::kSynReply:     return "SYN_REPLY";
        case SpdyFrameType::kRstStream:    return "RST_STREAM";
        case SpdyFrameType::kSettings:     return "SETTINGS";
        case SpdyFrameType::kPing:         return "PING";
        case SpdyFrameType::kGoAway:       return "GOAWAY";
        case SpdyFrameType::kHeaders:      return "HEADERS";
        case SpdyFrameType::kWindowUpdate: return "WINDOW_UPDATE";
        default: return "UNKNOWN";
    }
}

}  // namespace XER::Network::Spdy
