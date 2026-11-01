/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_response_headers.h"
#include "content/network/http/http_util.h"
#include <sstream>

namespace network::http {

void HttpResponseHeaders::Parse(const std::string& block,
                                 int* status_code,
                                 std::string* status_text) {
    std::istringstream ss(block);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            first = false;
            // "HTTP/1.1 200 OK"
            const size_t sp1 = line.find(' ');
            if (sp1 != std::string::npos) {
                const size_t sp2 = line.find(' ', sp1 + 1);
                if (status_code)
                    *status_code = std::stoi(line.substr(sp1+1, sp2-sp1-1));
                if (status_text && sp2 != std::string::npos)
                    *status_text = line.substr(sp2+1);
            }
            continue;
        }
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string name  = TrimWhitespace(line.substr(0, colon));
            const std::string value = TrimWhitespace(line.substr(colon+1));
            headers_[name] = value;
        }
    }
}

bool HttpResponseHeaders::Has(const std::string& name) const {
    return headers_.count(name) > 0;
}
std::string HttpResponseHeaders::Get(const std::string& name) const {
    const auto it = headers_.find(name);
    return it != headers_.end() ? it->second : "";
}
void HttpResponseHeaders::Set(const std::string& name, const std::string& value) {
    headers_[name] = value;
}
const std::map<std::string, std::string>& HttpResponseHeaders::All() const { return headers_; }

int HttpResponseHeaders::ContentLength() const {
    const auto it = headers_.find("Content-Length");
    if (it == headers_.end()) return -1;
    try { return std::stoi(it->second); } catch (...) { return -1; }
}

}  // namespace network::http
