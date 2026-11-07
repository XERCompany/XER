/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/url/url_request.h"
#include "content/network/url/url_request_context.h"

namespace XER::network::url {

// Convenience wrappers around URLRequestJob.
URLResponse SyncGet(const std::string& url,
                    const URLRequestContext& ctx = {},
                    std::string* error = nullptr);

URLResponse SyncPost(const std::string& url,
                     const std::string& body,
                     const std::string& content_type,
                     const URLRequestContext& ctx = {},
                     std::string* error = nullptr);

}  // namespace XER::network::url
