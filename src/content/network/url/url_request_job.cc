/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/url/url_request_job.h"

#include "content/network/network_util/network_util.h"
#include "content/network/socket/network_selector.h"
#include "content/network/socket/socket_stream.h"
#include "content/network/http/http_request_headers.h"
#include "content/network/http/http_response_headers.h"

namespace XER::network::url {

URLRequestJob::URLRequestJob(const URLRequestContext& context)
    : ctx_(context) {}

bool URLRequestJob::ParseURL(const std::string& url,
                              std::string* scheme,
                              std::string* host,
                              uint16_t* port,
                              std::string* path) const {
    const auto sep = url.find("://");
    if (sep == std::string::npos) return false;
    *scheme = XER::network::CanonicalizeScheme(url.substr(0, sep));
    std::string rest = url.substr(sep + 3);
    const auto slash = rest.find('/');
    std::string host_port;
    if (slash == std::string::npos) {
        host_port = rest; *path = "/";
    } else {
        host_port = rest.substr(0, slash);
        *path = rest.substr(slash);
    }
    std::string h; uint16_t p = 0;
    XER::network::SplitHostPort(host_port, &h, &p);
    *host = h;
    *port = p ? p : XER::network::DefaultPortForScheme(*scheme);
    return true;
}

bool URLRequestJob::Run(const URLRequest& request,
                        URLResponse* response,
                        std::string* error) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!ParseURL(request.url, &scheme, &host, &port, &path)) {
        if (error) *error = "invalid URL: " + request.url;
        return false;
    }

    XER::network::socket::NetworkSelector selector(ctx_.proxy_config);
    auto sock = selector.CreateSocket(scheme, host, port, error);
    if (!sock || !sock->Connect(host, port, error)) return false;

    // Build HTTP request
    http::HttpRequestHeaders hdrs;
    hdrs.Set("Host", host);
    hdrs.Set("User-Agent", ctx_.user_agent);
    hdrs.Set("Connection", "close");
    for (const auto& [k, v] : request.headers) hdrs.Set(k, v);

    const std::string req_line =
        request.method + " " + path + " HTTP/1.1\r\n" +
        hdrs.Serialize() + "\r\n";
    std::string wr_err;
    sock->Write(reinterpret_cast<const uint8_t*>(req_line.data()),
                req_line.size(), &wr_err);

    if (!request.body.empty())
        sock->Write(request.body.data(), request.body.size(), &wr_err);

    // Read response
    std::vector<uint8_t> raw;
    uint8_t buf[4096];
    int n;
    std::string rd_err;
    while ((n = sock->Read(buf, sizeof(buf), &rd_err)) > 0)
        raw.insert(raw.end(), buf, buf + n);

    if (raw.empty()) {
        if (error) *error = "empty response";
        return false;
    }
    const std::string raw_str(reinterpret_cast<char*>(raw.data()), raw.size());
    const size_t header_end = raw_str.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (error) *error = "malformed HTTP response";
        return false;
    }
    const std::string header_block = raw_str.substr(0, header_end);
    http::HttpResponseHeaders resp_hdrs;
    resp_hdrs.Parse(header_block, &response->status_code, &response->status_text);
    for (const auto& [k, v] : resp_hdrs.All()) response->headers[k] = v;
    response->body.assign(raw.begin() + static_cast<ptrdiff_t>(header_end + 4),
                          raw.end());
    return true;
}

}  // namespace XER::network::url
