/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_transaction.h"
#include "content/network/http/http_cache.h"
#include "content/network/url/url_request_context.h"
#include "content/network/url/url_request_job.h"
#include "async_io/io_thread_pool.h"

namespace network::http {

HttpTransactionResult HttpTransaction::Execute(const url::URLRequest& request) {
    // Attempt cache for GET
    if (request.method == "GET") {
        CachedResponse cached;
        if (HttpCache::Instance().Lookup(request.url, &cached)) {
            HttpTransactionResult res;
            res.ok                 = true;
            res.status_code        = cached.status_code;
            res.response.status_code = cached.status_code;
            res.response.headers   = cached.headers;
            res.response.body      = cached.body;
            return res;
        }
    }
    url::URLRequestContext ctx;
    url::URLRequestJob job(ctx);
    HttpTransactionResult res;
    res.ok = job.Run(request, &res.response, &res.error);
    res.status_code = res.response.status_code;
    res.status_text = res.response.status_text;
    return res;
}

void HttpTransaction::ExecuteAsync(const url::URLRequest& request,
                                    HttpTransactionCallback callback) {
    AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [this, request, cb = std::move(callback)]() mutable {
            cb(Execute(request));
        });
}

}  // namespace network::http
