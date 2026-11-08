/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_packet.h"
#include <cstring>

namespace XER::Network::Quic {

std::vector<uint8_t> SerializePacketHeader(const QuicPacketHeader& hdr) {
    std::vector<uint8_t> out;
    const uint8_t first = hdr.long_header ? 0xC0 : 0x40;
    out.push_back(first);
    if (hdr.long_header) {
        for (int i = 3; i >= 0; --i)
            out.push_back(static_cast<uint8_t>(hdr.version >> (i * 8)));
        // 8-byte dest/src CIDs
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>(hdr.dest_cid >> (i * 8)));
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>(hdr.src_cid >> (i * 8)));
    }
    // 4-byte packet number
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(hdr.packet_num >> (i * 8)));
    return out;
}

bool ParsePacketHeader(const uint8_t* data, size_t len,
                        QuicPacketHeader* hdr, size_t* consumed) {
    if (len < 1) return false;
    hdr->long_header = (data[0] & 0x80) != 0;
    size_t off = 1;
    if (hdr->long_header) {
        if (len < off + 4 + 8 + 8 + 4) return false;
        hdr->version = 0;
        for (int i = 0; i < 4; ++i)
            hdr->version = (hdr->version << 8) | data[off++];
        hdr->dest_cid = 0;
        for (int i = 0; i < 8; ++i)
            hdr->dest_cid = (hdr->dest_cid << 8) | data[off++];
        hdr->src_cid = 0;
        for (int i = 0; i < 8; ++i)
            hdr->src_cid = (hdr->src_cid << 8) | data[off++];
    } else {
        if (len < off + 4) return false;
    }
    hdr->packet_num = 0;
    for (int i = 0; i < 4; ++i)
        hdr->packet_num = (hdr->packet_num << 8) | data[off++];
    *consumed = off;
    return true;
}

}  // namespace XER::Network::Quic
