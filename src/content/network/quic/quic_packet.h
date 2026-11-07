/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <vector>
#include "content/network/quic/quic_protocol.h"

namespace XER::network::quic {

struct QuicPacketHeader {
    bool              long_header = false;
    uint32_t          version     = 0;
    QuicConnectionId  dest_cid    = 0;
    QuicConnectionId  src_cid     = 0;
    QuicPacketNumber  packet_num  = 0;
};

struct QuicPacket {
    QuicPacketHeader     header;
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> SerializePacketHeader(const QuicPacketHeader& hdr);
bool ParsePacketHeader(const uint8_t* data, size_t len,
                        QuicPacketHeader* hdr, size_t* consumed);

}  // namespace XER::network::quic
