/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "content/network/spdy/spdy_stream.h"

namespace XER::network::spdy {

class SpdySession {
public:
    SpdySession() = default;
    ~SpdySession() { Close(); }

    SpdyStream* CreateStream();
    SpdyStream* GetStream(SpdyStreamId id);
    void        CloseStream(SpdyStreamId id);
    void        Close();

    bool is_closed() const { return closed_; }

private:
    mutable std::mutex mu_;
    std::map<SpdyStreamId, std::unique_ptr<SpdyStream>> streams_;
    SpdyStreamId next_id_ = 1;
    bool         closed_  = false;
};

}  // namespace XER::network::spdy
