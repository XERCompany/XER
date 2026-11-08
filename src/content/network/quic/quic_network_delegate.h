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
    virtual void OnConnectionCreated(QuicConnectionId id, const std::string& host) {}
    virtual void OnConnectionClosed(QuicConnectionId id, QuicErrorCode code) {}
    virtual void OnStreamOpened(QuicConnectionId cid, QuicStreamId sid) {}
    virtual void OnStreamClosed(QuicConnectionId cid, QuicStreamId sid) {}
};

class DefaultQuicNetworkDelegate : public QuicNetworkDelegate {};

}  // namespace XER::Network::Quic
