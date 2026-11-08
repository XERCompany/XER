/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "content/network/quic/quic_protocol.h"

namespace XER::Network::Quic {

// QUIC protocol stream for multiplexed, reliable communication.
// Each QUIC connection can have multiple concurrent streams.
// Supports flow control, priority, and graceful reset.
// 
// Usage:
//   auto stream = connection.CreateStream();
//   stream->WriteData(data);
//   stream->OnDataAvailable = [](const std::vector<uint8_t>& data) { 
//       /* handle received data */ 
//   };
class QuicStream {
public:
    explicit QuicStream(QuicStreamId id) : id_(id) {}
    virtual ~QuicStream() = default;

    QuicStreamId id() const { return id_; }
    bool is_fin() const { return fin_; }

    virtual void OnDataAvailable(const uint8_t* data, size_t len, bool fin);
    virtual void Reset(QuicErrorCode code);

    const std::vector<uint8_t>& received_data() const { return recv_buf_; }

protected:
    QuicStreamId         id_;
    bool                 fin_      = false;
    std::vector<uint8_t> recv_buf_;
};

}  // namespace XER::Network::Quic
