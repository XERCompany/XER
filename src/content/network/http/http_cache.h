/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>

namespace XER::network::http {

struct CachedResponse {
    int                     status_code = 0;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t>    body;
    std::chrono::steady_clock::time_point expires;
};

class HttpCache {
public:
    static HttpCache& Instance();

    bool  Lookup(const std::string& key, CachedResponse* out) const;
    void  Store(const std::string& key, CachedResponse entry);
    void  Invalidate(const std::string& key);
    void  Clear();

private:
    HttpCache() = default;
    mutable std::mutex mu_;
    std::map<std::string, CachedResponse> store_;
};

}  // namespace XER::network::http
