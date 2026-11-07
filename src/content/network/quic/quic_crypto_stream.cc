/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_crypto_stream.h"

namespace XER::network::quic {

void QuicCryptoStream::WriteCryptoData(const uint8_t* data, size_t len) {
    recv_buf_.insert(recv_buf_.end(), data, data + len);
}

}  // namespace XER::network::quic
