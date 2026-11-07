/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/url/url_request_util.h"
#include "content/network/url/url_request_job.h"
#include "content/network/cache/network_cache_manager.h"
#include "content/network/logging/network_logger.h"

namespace XER::network::url {

URLResponse SyncGet(const std::string& url,
                    const URLRequestContext& ctx,
                    std::string* error) {
    logging::NetworkLogger::Instance().LogHttpRequest("GET", url, "XER");

    // Check cache first
    auto& cache_mgr = cache::NetworkCacheManager::Instance();
    auto cached = cache_mgr.GetCachedHttpResponse(url, error);
    if (cached) {
        logging::NetworkLogger::Instance().LogCacheHit("http_response", url);
        URLResponse resp;
        resp.status_code = cached->status_code;
        resp.body = cached->body;
        resp.headers = cached->headers.All();
        return resp;
    }

    logging::NetworkLogger::Instance().LogCacheMiss("http_response", url);

    URLRequest req;
    req.method = "GET";
    req.url    = url;
    URLResponse resp;
    std::string local_err;
    URLRequestJob job(ctx);
    job.Run(req, &resp, error ? error : &local_err);

    // Cache response if successful
    if (resp.status_code >= 200 && resp.status_code < 300) {
        cache::HttpResponseCacheEntry entry;
        entry.status_code = resp.status_code;
        entry.body = resp.body;
        // Convert headers back to HttpResponseHeaders format
        http::HttpResponseHeaders hdr;
        for (const auto& [key, value] : resp.headers) {
            // TODO: Need to set headers properly via HttpResponseHeaders API
        }
        entry.headers = hdr;
        cache_mgr.CacheHttpResponse(url, entry, error);
    }

    logging::NetworkLogger::Instance().LogHttpResponse("GET", url, resp.status_code, 0, resp.body.size());
    return resp;
}

URLResponse SyncPost(const std::string& url,
                     const std::string& body,
                     const std::string& content_type,
                     const URLRequestContext& ctx,
                     std::string* error) {
    logging::NetworkLogger::Instance().LogHttpRequest("POST", url, "XER");

    URLRequest req;
    req.method = "POST";
    req.url    = url;
    req.headers["Content-Type"] = content_type;
    req.body.assign(body.begin(), body.end());
    URLResponse resp;
    std::string local_err;
    URLRequestJob job(ctx);
    job.Run(req, &resp, error ? error : &local_err);

    logging::NetworkLogger::Instance().LogHttpResponse("POST", url, resp.status_code, 0, resp.body.size());
    return resp;
}

}  // namespace XER::network::url
