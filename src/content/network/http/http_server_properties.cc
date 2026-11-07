/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_server_properties.h"

namespace XER::network::http {

HttpServerProperties& HttpServerProperties::Instance() {
    static HttpServerProperties inst;
    return inst;
}

void HttpServerProperties::SetSupportsSPDY(const std::string& host, bool v) {
    std::lock_guard<std::mutex> lk(mu_);
    spdy_support_[host] = v;
}
bool HttpServerProperties::SupportsSPDY(const std::string& host) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = spdy_support_.find(host);
    return it != spdy_support_.end() && it->second;
}

void HttpServerProperties::SetRequiresHTTPS(const std::string& host, bool v) {
    std::lock_guard<std::mutex> lk(mu_);
    https_required_[host] = v;
}
bool HttpServerProperties::RequiresHTTPS(const std::string& host) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = https_required_.find(host);
    return it != https_required_.end() && it->second;
}

void HttpServerProperties::Clear() {
    std::lock_guard<std::mutex> lk(mu_);
    spdy_support_.clear();
    https_required_.clear();
}

}  // namespace XER::network::http
