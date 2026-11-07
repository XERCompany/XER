/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>

#include "content/network/url/url_request_context.h"
#include "content/network/http/http_network_session.h"
#include "content/network/http/http_cache.h"
#include "content/network/http/http_server_properties.h"
#include "content/network/dns/dns_config_service.h"
#include "content/network/cert/cert_storage.h"
#include "content/network/proxy/proxy_config.h"

namespace XER::network::core {

// Top-level network context that aggregates all subsystem singletons.
// Provides centralized access to HTTP session, DNS, proxy, certificate,
// and request configuration throughout the network module.
// 
// This is the main entry point for network operations. All components
// (HTTP, WebSocket, FTP, QUIC, etc.) should use this context.
// 
// Usage:
//   auto& ctx = NetworkContext::Instance();
//   auto& cache = ctx.Cache();
//   auto& session = ctx.HttpSession();
class NetworkContext {
public:
    // Get the singleton network context.
    // Creates the instance on first call.
    static NetworkContext& Instance();

    // Get the URL request context (contains cookies, proxy config, timeout, etc.).
    url::URLRequestContext&          RequestContext()     { return request_ctx_; }
    
    // Get the HTTP network session for managing HTTP/2 connections.
    http::HttpNetworkSession&        HttpSession()        { return http::HttpNetworkSession::Default(); }
    
    // Get the HTTP cache for storing responses.
    http::HttpCache&                 Cache()              { return http::HttpCache::Instance(); }
    
    // Get the HTTP server properties cache (alternate protocols, etc.).
    http::HttpServerProperties&      ServerProperties()   { return http::HttpServerProperties::Instance(); }

    // Reset all network state (for cleanup or restart).
    // Should be called before isolate shutdown to clean up resources.
    void Reset();

private:
    NetworkContext() = default;
    url::URLRequestContext request_ctx_;
};

}  // namespace XER::network::core
