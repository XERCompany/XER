/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/proxy/proxy_service.h"

#include "content/network/network_util/network_util.h"

namespace XER::Network::Proxy {

ProxyService::ProxyService(const ProxyConfig& config) : config_(config) {}

bool ProxyService::Resolve(const std::string& url,
                           ProxyInfo* info,
                           std::string* error) {
    // Extract host from URL
    std::string work = url;
    const auto sep = work.find("://");
    if (sep != std::string::npos) work = work.substr(sep + 3);
    std::string host;
    uint16_t port = 0;
    XER::Network::SplitHostPort(work, &host, &port);
    // Strip path
    const auto slash = host.find('/');
    if (slash != std::string::npos) host = host.substr(0, slash);

    if (config_.proxy.type == ProxyType::kDirect || IsBypassed(host)) {
        *info = ProxyInfo::Direct();
        return true;
    }
    *info = ProxyInfo::Via(config_.proxy);
    return true;
}

bool ProxyService::IsBypassed(const std::string& host) const {
    for (const auto& rule : config_.bypass_list) {
        if (rule == host) return true;
        if (!rule.empty() && rule[0] == '*' &&
            host.size() >= rule.size() - 1 &&
            host.substr(host.size() - (rule.size() - 1)) == rule.substr(1))
            return true;
    }
    return false;
}

}  // namespace XER::Network::Proxy
