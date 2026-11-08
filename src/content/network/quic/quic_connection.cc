/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_connection.h"

namespace XER::Network::Quic {

QuicConnection::QuicConnection(QuicConnectionId id,
                                 const std::string& peer_host,
                                 uint16_t peer_port)
    : id_(id), peer_host_(peer_host), peer_port_(peer_port) {}

QuicConnection::~QuicConnection() { Close(); }

QuicStream* QuicConnection::OpenStream(QuicStreamId id) {
    auto s = std::make_unique<QuicStream>(id);
    QuicStream* ptr = s.get();
    streams_[id] = std::move(s);
    return ptr;
}

QuicStream* QuicConnection::GetStream(QuicStreamId id) {
    const auto it = streams_.find(id);
    return it != streams_.end() ? it->second.get() : nullptr;
}

void QuicConnection::CloseStream(QuicStreamId id) {
    streams_.erase(id);
}

void QuicConnection::Close(QuicErrorCode /*code*/) {
    state_ = QuicConnectionState::kClosed;
    streams_.clear();
}

}  // namespace XER::Network::Quic
