/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace XER::network::http {

// Synchronous one-shot HTTP request over libcurl.
// Shared transport for callers that need a bounded, blocking
// request/response exchange (NodeGraph net.* nodes, S3 client, etc.).
// The Chromium-style async stack (url_request / http_transaction) is for
// long-lived sessions; this helper is the canonical sync path.
struct SyncResponse {
    long status = 0;                                 // HTTP status, 0 on transport error
    std::string body;                                // response body (binary-safe)
    std::map<std::string, std::string> headers;      // response headers, lowercase names
    std::string error;                               // curl error string when !transport_ok
    bool transport_ok = false;                       // request completed (any HTTP status)
};

// `headers` entries are "Name: value" lines sent verbatim.
// `timeout_ms` bounds the whole exchange; connect timeout is capped at 15s.
// HEAD requests automatically suppress the response body.
SyncResponse SyncRequest(const std::string& method,
                         const std::string& url,
                         const std::string& body = {},
                         const std::vector<std::string>& headers = {},
                         long timeout_ms = 60000,
                         const std::string& user_agent = "XER-NodeGraph/1.0");

}  // namespace XER::network::http
