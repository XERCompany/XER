/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <string>
#include "content/network/url/url_request.h"

namespace XER::Network::Http {

struct HttpTransactionResult {
    bool        ok = false;
    int         status_code = 0;
    std::string status_text;
    std::string error;
    Url::URLResponse response;
};

using HttpTransactionCallback = std::function<void(HttpTransactionResult)>;

// Executes a URLRequest through the HTTP layer (cache-aware).
class HttpTransaction {
public:
    // Synchronous execute.
    HttpTransactionResult Execute(const Url::URLRequest& request);

    // Asynchronous execute via IOThreadPool.
    void ExecuteAsync(const Url::URLRequest& request,
                      HttpTransactionCallback callback);
};

}  // namespace XER::Network::Http
