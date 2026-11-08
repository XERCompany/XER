/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_stream.h"

namespace XER::Network::Quic {

void QuicStream::OnDataAvailable(const uint8_t* data, size_t len, bool fin) {
    recv_buf_.insert(recv_buf_.end(), data, data + len);
    fin_ = fin;
}

void QuicStream::Reset(QuicErrorCode /*code*/) {
    recv_buf_.clear();
    fin_ = true;
}

}  // namespace XER::Network::Quic
