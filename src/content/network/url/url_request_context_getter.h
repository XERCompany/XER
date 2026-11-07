/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include "content/network/url/url_request_context.h"

namespace XER::network::url {

// Lazily constructs and caches a URLRequestContext.
class URLRequestContextGetter {
public:
    URLRequestContextGetter() = default;
    explicit URLRequestContextGetter(URLRequestContext ctx);

    const URLRequestContext& context() const;
    void SetContext(URLRequestContext ctx);

private:
    mutable std::unique_ptr<URLRequestContext> ctx_;
};

}  // namespace XER::network::url
