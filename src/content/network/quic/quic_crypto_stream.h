/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include <vector>
#include "content/network/quic/quic_stream.h"

namespace XER::network::quic {

// Handles CRYPTO frames (TLS handshake messages).
class QuicCryptoStream : public QuicStream {
public:
    QuicCryptoStream() : QuicStream(0xFFFFFFFF) {}

    void WriteCryptoData(const uint8_t* data, size_t len);
    bool HandshakeComplete() const { return handshake_complete_; }
    void SetHandshakeComplete(bool v) { handshake_complete_ = v; }

private:
    bool handshake_complete_ = false;
};

}  // namespace XER::network::quic
