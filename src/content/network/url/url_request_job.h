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

    bool Run(const URLRequest& request,
             URLResponse* response,
             std::string* error);
             
    bool ParseURL(const std::string& url,
                  std::string* scheme,
                  std::string* host,
                  uint16_t* port,
                  std::string* path) const;

private:

    const URLRequestContext& ctx_;
};

}  // namespace XER::Network::Url
