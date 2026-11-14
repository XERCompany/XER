/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/quic/quic_protocol.h"

namespace XER::Network::Quic {

class QuicNetworkDelegate {
public:
    virtual ~QuicNetworkDelegate() = default;
    virtual void OnConnectionCreated(QuicConnectionId, const std::string&) {}
    virtual void OnConnectionClosed(QuicConnectionId, QuicErrorCode) {}
    virtual void OnStreamOpened(QuicConnectionId, QuicStreamId) {}
    virtual void OnStreamClosed(QuicConnectionId, QuicStreamId) {}
};

class DefaultQuicNetworkDelegate : public QuicNetworkDelegate {};

}  // namespace XER::Network::Quic
