/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/url/url_request_context_getter.h"

namespace XER::network::url {

URLRequestContextGetter::URLRequestContextGetter(URLRequestContext ctx)
    : ctx_(std::make_unique<URLRequestContext>(std::move(ctx))) {}

const URLRequestContext& URLRequestContextGetter::context() const {
    if (!ctx_) ctx_ = std::make_unique<URLRequestContext>();
    return *ctx_;
}

void URLRequestContextGetter::SetContext(URLRequestContext ctx) {
    ctx_ = std::make_unique<URLRequestContext>(std::move(ctx));
}

}  // namespace XER::network::url
