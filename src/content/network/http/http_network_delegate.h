/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "content/network/url/url_request.h"

namespace XER::network::http {

// Observer interface for HTTP network events.
class HttpNetworkDelegate {
public:
    virtual ~HttpNetworkDelegate() = default;

    virtual void OnBeforeRequest(const url::URLRequest& req) {}
    virtual void OnResponseStarted(int status_code) {}
    virtual void OnCompleted(bool ok, const std::string& error) {}
    virtual void OnRedirect(const std::string& new_url) {}
};

class DefaultHttpNetworkDelegate : public HttpNetworkDelegate {};

}  // namespace XER::network::http
