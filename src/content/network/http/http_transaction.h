/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <string>
#include "content/network/url/url_request.h"

namespace XER::network::http {

struct HttpTransactionResult {
    bool        ok = false;
    int         status_code = 0;
    std::string status_text;
    std::string error;
    url::URLResponse response;
};

using HttpTransactionCallback = std::function<void(HttpTransactionResult)>;

// Executes a URLRequest through the HTTP layer (cache-aware).
class HttpTransaction {
public:
    // Synchronous execute.
    HttpTransactionResult Execute(const url::URLRequest& request);

    // Asynchronous execute via IOThreadPool.
    void ExecuteAsync(const url::URLRequest& request,
                      HttpTransactionCallback callback);
};

}  // namespace XER::network::http
