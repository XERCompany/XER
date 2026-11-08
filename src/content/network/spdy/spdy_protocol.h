/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::Network::Spdy {

using SpdyStreamId  = uint32_t;
using SpdySessionId = uint32_t;

static constexpr uint8_t  kSpdyVersion3   = 3;
static constexpr uint16_t kDefaultSpdyPort = 443;

enum class SpdyFrameType : uint8_t {
    kData       = 0,
    kSynStream  = 1,
    kSynReply   = 2,
    kRstStream  = 3,
    kSettings   = 4,
    kPing       = 6,
    kGoAway     = 7,
    kHeaders    = 8,
    kWindowUpdate = 9,
};

enum class SpdyRstStreamStatus : uint32_t {
    kNoError            = 0,
    kProtocolError      = 1,
    kInvalidStream      = 2,
    kRefusedStream      = 3,
    kUnsupportedVersion = 4,
    kCancel             = 5,
    kInternalError      = 6,
    kFlowControlError   = 7,
};

std::string SpdyFrameTypeToString(SpdyFrameType t);

}  // namespace XER::Network::Spdy
