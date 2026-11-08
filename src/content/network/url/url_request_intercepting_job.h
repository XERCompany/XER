/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <string>
#include "content/network/url/url_request.h"

namespace XER::Network::Url {

using InterceptorFn = std::function<bool(const URLRequest& req,
                                         URLResponse* resp)>;

// Wraps URLRequestJob with an intercept hook (for mocking / caching layers).
class URLRequestInterceptingJob {
public:
    void SetInterceptor(InterceptorFn fn) { interceptor_ = std::move(fn); }
    void ClearInterceptor() { interceptor_ = nullptr; }

    // Returns true if the interceptor handled the request.
    bool TryIntercept(const URLRequest& req, URLResponse* resp) const {
        return interceptor_ && interceptor_(req, resp);
    }

private:
    InterceptorFn interceptor_;
};

}  // namespace XER::Network::Url
