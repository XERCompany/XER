/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include "content/network/quic/quic_protocol.h"
#include "content/network/quic/quic_stream.h"
#include "content/network/quic/quic_crypto_stream.h"

namespace XER::network::quic {

enum class QuicConnectionState {
    kIdle, kHandshaking, kConnected, kClosing, kClosed
};

class QuicConnection {
public:
    QuicConnection(QuicConnectionId id, const std::string& peer_host, uint16_t peer_port);
    ~QuicConnection();

    QuicConnectionId  id()    const { return id_; }
    QuicConnectionState state() const { return state_; }

    QuicStream* OpenStream(QuicStreamId id);
    QuicStream* GetStream(QuicStreamId id);
    void        CloseStream(QuicStreamId id);

    void Close(QuicErrorCode code = QuicErrorCode::kNoError);

private:
    QuicConnectionId  id_;
    std::string       peer_host_;
    uint16_t          peer_port_ = 443;
    QuicConnectionState state_   = QuicConnectionState::kIdle;
    std::map<QuicStreamId, std::unique_ptr<QuicStream>> streams_;
    QuicCryptoStream  crypto_stream_;
};

}  // namespace XER::network::quic
