/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/url/url_request_job.h"

#include "content/network/network_util/network_util.h"
#include "content/network/socket/network_selector.h"
#include "content/network/socket/socket_stream.h"
#include "content/network/socket/ssl_client_socket.h"
#include "content/network/http/http_request_headers.h"
#include "content/network/http/http_response_headers.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace XER::Network::Url {
namespace {

// Case-insensitive header lookup over the parsed response map —
// HttpResponseHeaders preserves the wire casing.
std::string HeaderValue(const URLResponse& r, const std::string& name) {
    for (const auto& [k, v] : r.headers) {
        if (k.size() == name.size() &&
            std::equal(k.begin(), k.end(), name.begin(),
                       [](char a, char b) {
                           return std::tolower(
                                      static_cast<unsigned char>(a)) ==
                                  std::tolower(
                                      static_cast<unsigned char>(b));
                       })) {
            return v;
        }
    }
    return {};
}

bool IsRedirectStatus(int status) {
    switch (status) {
        case 301: case 302: case 303: case 307: case 308:
            return true;
        default:
            return false;
    }
}

// Resolves a Location header against the current URL. Handles absolute
// URLs, scheme-relative "//host/path", root-relative "/path" and plain
// relative names resolved against the current directory.
std::string ResolveRedirect(const std::string& base_url,
                            const std::string& location) {
    if (location.empty()) return {};
    if (location.find("://") != std::string::npos) return location;
    const auto sep = base_url.find("://");
    if (sep == std::string::npos) return {};
    const std::string scheme = base_url.substr(0, sep);
    std::string rest = base_url.substr(sep + 3);
    const auto slash = rest.find('/');
    const std::string authority =
        slash == std::string::npos ? rest : rest.substr(0, slash);
    if (location.rfind("//", 0) == 0) {
        return scheme + ":" + location;
    }
    if (location.front() == '/') {
        return scheme + "://" + authority + location;
    }
    std::string dir =
        slash == std::string::npos
            ? "/"
            : rest.substr(0, rest.find_last_of('/') + 1);
    return scheme + "://" + authority + dir + location;
}

// Decodes a Transfer-Encoding: chunked body. Returns false on a
// truncated/malformed chunk — callers treat that as a transport error.
bool DecodeChunked(const std::vector<uint8_t>& in,
                   std::vector<uint8_t>* out) {
    out->clear();
    size_t pos = 0;
    const char* kSep = "\r\n";
    while (true) {
        const auto eol = std::search(in.begin() + pos, in.end(),
                                     kSep, kSep + 2) -
                         in.begin();
        if (static_cast<size_t>(eol) >= in.size()) return false;
        size_t chunk_size = 0;
        const char* p = reinterpret_cast<const char*>(in.data() + pos);
        const char* pe = reinterpret_cast<const char*>(in.data() + eol);
        const auto res = std::from_chars(p, pe, chunk_size, 16);
        if (res.ec != std::errc{}) return false;
        pos = static_cast<size_t>(eol) + 2;
        if (chunk_size == 0) return true;   // trailers ignored
        if (pos + chunk_size + 2 > in.size()) return false;
        out->insert(out->end(), in.begin() + pos,
                    in.begin() + pos + chunk_size);
        pos += chunk_size;
        if (in[pos] != '\r' || in[pos + 1] != '\n') return false;
        pos += 2;
    }
}

}  // namespace

URLRequestJob::URLRequestJob(const URLRequestContext& context)
    : ctx_(context) {}

bool URLRequestJob::ParseURL(const std::string& url,
                              std::string* scheme,
                              std::string* host,
                              uint16_t* port,
                              std::string* path) const {
    const auto sep = url.find("://");
    if (sep == std::string::npos) return false;
    *scheme = XER::Network::CanonicalizeScheme(url.substr(0, sep));
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
    XER::Network::SplitHostPort(host_port, &h, &p);
    *host = h;
    *port = p ? p : XER::Network::DefaultPortForScheme(*scheme);
    return true;
}

// Single request/response exchange — Run() wraps this in the redirect
// loop so Location handling stays in one place.
bool URLRequestJob::RunOnce(const URLRequest& request,
                            URLResponse* response,
                            std::string* error) {
    std::string scheme, host, path;
    uint16_t port = 0;
    if (!ParseURL(request.url, &scheme, &host, &port, &path)) {
        if (error) *error = "invalid URL: " + request.url;
        return false;
    }

    XER::Network::Socket::NetworkSelector selector(ctx_.proxy_config);
    auto sock = selector.CreateSocket(scheme, host, port, error);
    if (!sock) return false;

    // Push the context's TLS policy into the socket before connecting —
    // this is where the certificate storage and verify flags the caller
    // placed in URLRequestContext actually take effect. The SNI name
    // must match the handshake host for proper cert validation.
    if (auto* ssl =
            dynamic_cast<XER::Network::Socket::SslClientSocket*>(
                sock.get())) {
        ssl->SetSkipVerify(ctx_.skip_ssl_verify);
        ssl->SetSniHostname(host);
        if (!ctx_.cert_storage.bundle_path().empty()) {
            ssl->SetCaBundlePath(ctx_.cert_storage.bundle_path());
        }
    }
    if (!sock->Connect(host, port, error)) return false;

    // Build HTTP request
    Http::HttpRequestHeaders hdrs;
    hdrs.Set("Host", host);
    hdrs.Set("User-Agent", ctx_.user_agent);
    hdrs.Set("Connection", "close");
    for (const auto& [k, v] : request.headers) hdrs.Set(k, v);
    if (!request.body.empty()) {
        hdrs.Set("Content-Length", std::to_string(request.body.size()));
    }

    const std::string req_line =
        request.method + " " + path + " HTTP/1.1\r\n" +
        hdrs.Serialize() + "\r\n";
    std::string wr_err;
    sock->Write(reinterpret_cast<const uint8_t*>(req_line.data()),
                req_line.size(), &wr_err);

    if (!request.body.empty())
        sock->Write(request.body.data(), request.body.size(), &wr_err);

    // Read response (Connection: close -> read until EOF).
    std::vector<uint8_t> raw;
    uint8_t buf[8192];
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
    Http::HttpResponseHeaders resp_hdrs;
    resp_hdrs.Parse(header_block, &response->status_code, &response->status_text);
    for (const auto& [k, v] : resp_hdrs.All()) response->headers[k] = v;

    std::vector<uint8_t> body(
        raw.begin() + static_cast<ptrdiff_t>(header_end + 4),
        raw.end());

    // Chunked framing must be decoded — otherwise callers receive the
    // wire framing bytes mixed into the payload.
    std::string te = HeaderValue(*response, "Transfer-Encoding");
    std::transform(te.begin(), te.end(), te.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (te.find("chunked") != std::string::npos) {
        if (!DecodeChunked(body, &response->body)) {
            if (error) *error = "malformed chunked response body";
            return false;
        }
    } else {
        response->body = std::move(body);
    }
    return true;
}

bool URLRequestJob::Run(const URLRequest& request,
                        URLResponse* response,
                        std::string* error,
                        std::string* effective_url) {
    URLRequest cur = request;
    const int max_hops =
        request.follow_redirects ? request.max_redirects : 0;
    for (int hop = 0;; ++hop) {
        if (!RunOnce(cur, response, error)) {
            if (effective_url) *effective_url = cur.url;
            return false;
        }
        if (hop >= max_hops ||
            !IsRedirectStatus(response->status_code)) {
            if (effective_url) *effective_url = cur.url;
            return true;
        }
        const std::string location =
            HeaderValue(*response, "Location");
        const std::string next = ResolveRedirect(cur.url, location);
        if (next.empty()) {
            if (error) {
                *error = "redirect " +
                         std::to_string(response->status_code) +
                         " without a usable Location";
            }
            return false;
        }
        cur.url = next;
        // 301/302/303 rewrite the method to GET for non-GET requests —
        // the body must not be re-sent to the new location.
        if (response->status_code == 303 ||
            ((response->status_code == 301 ||
              response->status_code == 302) &&
             cur.method != "GET" && cur.method != "HEAD")) {
            cur.method = "GET";
            cur.body.clear();
        }
        response->headers.clear();
        response->body.clear();
    }
}

}  // namespace XER::Network::Url
