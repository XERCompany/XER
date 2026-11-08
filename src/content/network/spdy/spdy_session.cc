/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/spdy/spdy_session.h"

namespace XER::Network::Spdy {

SpdyStream* SpdySession::CreateStream() {
    std::lock_guard<std::mutex> lk(mu_);
    const SpdyStreamId id = next_id_++;
    auto s = std::make_unique<SpdyStream>(id);
    SpdyStream* ptr = s.get();
    streams_[id] = std::move(s);
    return ptr;
}

SpdyStream* SpdySession::GetStream(SpdyStreamId id) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = streams_.find(id);
    return it != streams_.end() ? it->second.get() : nullptr;
}

void SpdySession::CloseStream(SpdyStreamId id) {
    std::lock_guard<std::mutex> lk(mu_);
    streams_.erase(id);
}

void SpdySession::Close() {
    std::lock_guard<std::mutex> lk(mu_);
    streams_.clear();
    closed_ = true;
}

}  // namespace XER::Network::Spdy
