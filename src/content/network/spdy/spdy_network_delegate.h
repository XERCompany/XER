/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/spdy/spdy_protocol.h"

namespace XER::network::spdy {

class SpdyNetworkDelegate {
public:
    virtual ~SpdyNetworkDelegate() = default;
    virtual void OnSessionCreated(const std::string& host) {}
    virtual void OnSessionClosed(const std::string& host) {}
    virtual void OnStreamOpened(SpdyStreamId id) {}
    virtual void OnStreamClosed(SpdyStreamId id) {}
};

class DefaultSpdyNetworkDelegate : public SpdyNetworkDelegate {};

}  // namespace XER::network::spdy
