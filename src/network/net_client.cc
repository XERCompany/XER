/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "network/net_client.h"

#include "content/network/url/url_request.h"
#include "content/network/url/url_request_context.h"
#include "content/network/url/url_request_job.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace XER::Network {
namespace {

NetResponse Rejected(std::string reason) {
    NetResponse r;
    r.error = "net_client: " + std::move(reason);
    return r;
}

}  // namespace

NetResponse Fetch(const std::string& method, const std::string& url,
                  const std::string& body,
                  const std::vector<std::string>& headers,
                  long timeout_ms) {
    if (url.empty()) return Rejected("url is empty");
    if (method.empty()) return Rejected("method is empty");
    if (timeout_ms <= 0) return Rejected("timeout_ms must be positive");

    // The verb is compared literally for body handling, so normalize
    // once here instead of pushing case handling onto every caller.
    std::string verb = method;
    std::transform(verb.begin(), verb.end(), verb.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Strictly the src/ URL job — cert verification rides on the
    // repository's SslContext/CertStorage stack (Windows root store on
    // this platform), not on any third-party HTTP client.
    Url::URLRequestContext ctx;
    ctx.user_agent = "XER-NetClient/1.0";
    ctx.socket_timeout_ms = static_cast<int>(timeout_ms);

    Url::URLRequest req;
    req.method = verb;
    req.url = url;
    req.timeout_ms = static_cast<uint32_t>(timeout_ms);
    for (const std::string& h : headers) {
        const auto colon = h.find(':');
        if (colon == std::string::npos) continue;
        std::string name = h.substr(0, colon);
        std::string value = h.substr(colon + 1);
        while (!value.empty() &&
               (value.front() == ' ' || value.front() == '\t')) {
            value.erase(0, 1);
        }
        if (!name.empty()) req.headers[name] = value;
    }
    req.body.assign(body.begin(), body.end());

    Url::URLRequestJob job(ctx);
    Url::URLResponse resp;
    std::string err;
    const bool ok = job.Run(req, &resp, &err);

    NetResponse out;
    out.status = resp.status_code;
    out.body.assign(resp.body.begin(), resp.body.end());
    out.headers = resp.headers;
    out.ok = ok;
    if (!err.empty()) {
        out.error = verb + " " + url + ": " + err;
    }
    return out;
}

NetResponse Get(const std::string& url, long timeout_ms) {
    return Fetch("GET", url, {}, {}, timeout_ms);
}

NetResponse Post(const std::string& url, const std::string& body,
                 const std::string& content_type, long timeout_ms) {
    std::vector<std::string> headers;
    if (!content_type.empty()) {
        headers.push_back("Content-Type: " + content_type);
    }
    return Fetch("POST", url, body, headers, timeout_ms);
}

bool DownloadFile(const std::string& url, const std::string& dest_path,
                  long timeout_ms, std::string* error_out) {
    auto fail = [error_out](std::string msg) {
        if (error_out != nullptr) *error_out = std::move(msg);
        return false;
    };

    if (dest_path.empty()) {
        return fail("net_client: dest_path is empty");
    }

    const NetResponse r = Get(url, timeout_ms);
    if (!r.ok) {
        return fail(r.error);  // Fetch already adds method + url context
    }
    if (r.status >= 400) {
        return fail("GET " + url + ": HTTP " + std::to_string(r.status));
    }

    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return fail("net_client: cannot open " + dest_path +
                    " for writing");
    }
    out.write(r.body.data(), static_cast<std::streamsize>(r.body.size()));
    if (!out.good()) {
        return fail("net_client: write failed for " + dest_path);
    }
    return true;
}

}  // namespace XER::Network
