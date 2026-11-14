/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/spdy/spdy_protocol.h"

namespace XER::Network::Spdy {

class SpdyNetworkDelegate {
public:
    virtual ~SpdyNetworkDelegate() = default;
    virtual void OnSessionCreated(const std::string&) {}
    virtual void OnSessionClosed(const std::string&) {}
    virtual void OnStreamOpened(SpdyStreamId) {}
    virtual void OnStreamClosed(SpdyStreamId) {}
};

class DefaultSpdyNetworkDelegate : public SpdyNetworkDelegate {};

}  // namespace XER::Network::Spdy
