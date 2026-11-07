/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include "content/network/url/url_request_context.h"
#include "content/network/http/http_cache.h"

namespace XER::network::http {

// Aggregates context objects used across an HTTP session.
struct HttpNetworkSession {
    url::URLRequestContext request_context;

    static HttpNetworkSession& Default();
};

}  // namespace XER::network::http
