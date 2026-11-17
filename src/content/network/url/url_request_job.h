/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/url/url_request.h"
#include "content/network/url/url_request_context.h"

namespace XER::Network::Url {

// Executes a single URLRequest synchronously.
class URLRequestJob {
public:
    explicit URLRequestJob(const URLRequestContext& context);

    // Executes the request synchronously — follows 3xx redirects up to
    // request.max_redirects when request.follow_redirects is set,
    // decodes chunked bodies, and applies the context's TLS policy
    // (cert storage, skip_ssl_verify, SNI) to the connection.
    bool Run(const URLRequest& request,
             URLResponse* response,
             std::string* error);

    bool ParseURL(const std::string& url,
                  std::string* scheme,
                  std::string* host,
                  uint16_t* port,
                  std::string* path) const;

private:
    // Single request/response exchange — Run() wraps it in the
    // redirect loop.
    bool RunOnce(const URLRequest& request,
                 URLResponse* response,
                 std::string* error);

    const URLRequestContext& ctx_;
};

}  // namespace XER::Network::Url
