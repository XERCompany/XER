/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_request_headers.h"
#include <sstream>

namespace XER::Network::Http {

void HttpRequestHeaders::Set(const std::string& name, const std::string& value) {
    headers_[name] = value;
}
bool HttpRequestHeaders::Has(const std::string& name) const {
    return headers_.count(name) > 0;
}
std::string HttpRequestHeaders::Get(const std::string& name) const {
    const auto it = headers_.find(name);
    return it != headers_.end() ? it->second : "";
}
void HttpRequestHeaders::Remove(const std::string& name) { headers_.erase(name); }
void HttpRequestHeaders::Clear() { headers_.clear(); }
const std::map<std::string, std::string>& HttpRequestHeaders::All() const { return headers_; }
std::string HttpRequestHeaders::Serialize() const {
    std::string out;
    for (const auto& [k, v] : headers_) { out += k; out += ": "; out += v; out += "\r\n"; }
    return out;
}

}  // namespace XER::Network::Http
