/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::network::quic {

using QuicConnectionId   = uint64_t;
using QuicStreamId       = uint32_t;
using QuicPacketNumber   = uint64_t;
using QuicByteCount      = uint64_t;

static constexpr uint32_t kQuicVersion1 = 0x00000001u;
static constexpr uint16_t kDefaultQuicPort = 443;

enum class QuicFrameType : uint8_t {
    kPadding          = 0x00,
    kPing             = 0x01,
    kAck              = 0x02,
    kResetStream      = 0x04,
    kConnectionClose  = 0x1c,
    kStream           = 0x08,
    kCrypto           = 0x06,
};

enum class QuicErrorCode : uint32_t {
    kNoError             = 0,
    kInternalError       = 1,
    kStreamDataAfterFin  = 2,
    kConnectionTimeout   = 3,
    kCryptoError         = 4,
};

std::string QuicErrorCodeToString(QuicErrorCode code);

}  // namespace XER::network::quic
