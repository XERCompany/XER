/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "content/network/spdy/spdy_protocol.h"

namespace XER::network::spdy {

struct SpdyFrame {
    SpdyFrameType  type       = SpdyFrameType::kData;
    bool           control    = false;
    SpdyStreamId   stream_id  = 0;
    bool           fin        = false;
    std::vector<uint8_t> data;
    std::map<std::string, std::string> headers;
};

// Serialise a SPDY control/data frame.
std::vector<uint8_t> SerializeSpdyFrame(const SpdyFrame& frame);

// Parse the next frame from |buf|. Returns bytes consumed, 0 if incomplete.
size_t ParseSpdyFrame(const uint8_t* buf, size_t len, SpdyFrame* out);

}  // namespace XER::network::spdy
