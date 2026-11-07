/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "content/network/spdy/spdy_protocol.h"

namespace XER::network::spdy {

using SpdyDataCallback = std::function<void(const uint8_t*, size_t, bool fin)>;

class SpdyStream {
public:
    explicit SpdyStream(SpdyStreamId id) : id_(id) {}
    virtual ~SpdyStream() = default;

    SpdyStreamId id() const { return id_; }
    bool         fin()  const { return fin_; }

    void SetOnData(SpdyDataCallback cb) { on_data_ = std::move(cb); }
    void OnDataReceived(const uint8_t* data, size_t len, bool fin);
    void Reset(SpdyRstStreamStatus status);

    const std::map<std::string, std::string>& request_headers() const { return req_hdrs_; }
    void SetRequestHeaders(std::map<std::string, std::string> hdrs) { req_hdrs_ = std::move(hdrs); }

private:
    SpdyStreamId                        id_;
    bool                                fin_ = false;
    SpdyDataCallback                    on_data_;
    std::map<std::string, std::string>  req_hdrs_;
};

}  // namespace XER::network::spdy
