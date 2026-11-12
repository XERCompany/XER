/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * Internal HTTP/1.1 streaming helpers for the model-hub clients.
 * Not part of the public API — do not include from outside this module.
 */
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace content::civitai::internal {

namespace detail {
struct HttpStreamImpl;  // defined in model_hub_http.cc
}  // namespace detail

// ---------------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------------

struct ParsedUrl {
    std::string scheme;   // "https" | "http"
    std::string host;
    std::string path;     // "/a/b" — empty becomes "/"
    std::string query;    // without '?'
    uint16_t    port = 0; // resolved default when 0
};

// Parses absolute http(s) URLs. Returns false for malformed input.
bool ParseUrl(std::string_view url, ParsedUrl* out);

// Percent-encoding helpers for query parameters.
std::string UrlEncode(std::string_view value);
std::string UrlDecode(std::string_view value);

// ---------------------------------------------------------------------------
// Streaming HTTP client
// ---------------------------------------------------------------------------

// Minimal HTTP/1.1 client over XER::Network sockets. One request per Open();
// the body is consumed through Read() so multi-GB payloads never sit in
// memory. Handles Content-Length, chunked transfer-encoding and
// close-delimited bodies, plus redirect following for the initial request.
//
// TLS: https URLs use Socket::SslClientSocket (handshake inside Connect()).
// Plain http uses Socket::TcpClientSocket.
class HttpStream {
public:
    struct ResponseInfo {
        int         status = 0;
        std::string status_text;
        std::string final_url;   // after redirects
        // Lowercase header names.
        std::map<std::string, std::string> headers;
    };

    HttpStream();
    ~HttpStream();
    HttpStream(const HttpStream&) = delete;
    HttpStream& operator=(const HttpStream&) = delete;

    // TLS options applied to https connections (ignored for http).
    // Must be called before Open(). Empty ca_bundle = OpenSSL defaults.
    void SetCaBundlePath(const std::string& path);
    void SetSkipVerify(bool skip);

    // Sends the request and reads response headers. |range| is an optional
    // Range header value (e.g. "bytes=1024-"); empty = no Range header.
    // Follows up to |max_redirects| 3xx responses internally (Authorization
    // is dropped when the redirect crosses hosts).
    // Returns false on transport/protocol errors; check Error().
    bool Open(std::string_view url,
              const std::map<std::string, std::string>& extra_headers,
              std::string_view range,
              int max_redirects,
              std::string* error);

    const ResponseInfo& Info() const { return info_; }

    // Total body size when known: Content-Length, or the total from a
    // Content-Range header (206 responses). 0 = unknown.
    uint64_t ExpectedBodyBytes() const;

    // Bytes already delivered by the server for a 206 response (resume
    // offset). 0 for 200/close-delimited bodies.
    uint64_t ResumeOffset() const;

    // Reads up to |len| decoded body bytes.
    // Returns >0 bytes read, 0 on clean EOF, -1 on error (see Error()).
    int ReadBody(uint8_t* buf, size_t len);

    const std::string& Error() const { return error_; }
    void Close();

private:
    std::unique_ptr<detail::HttpStreamImpl> impl_;
    ResponseInfo info_;
    std::string error_;
};

// Locates a PEM CA bundle for TLS verification. Checks XER_CA_BUNDLE and
// SSL_CERT_FILE env vars, then well-known install locations. Returns ""
// when nothing is found (OpenSSL default verify paths apply).
std::string DefaultCaBundlePath();

// ---------------------------------------------------------------------------
// Small buffered GET for JSON APIs
// ---------------------------------------------------------------------------

// Performs a GET and buffers the whole body (capped at |max_bytes|).
// Returns false on transport errors or non-2xx status when
// |allow_error_status| is false. Body is returned as bytes.
bool HttpGet(std::string_view url,
             const std::map<std::string, std::string>& headers,
             std::string* body_out,
             int* status_out,
             size_t max_bytes = 64u * 1024u * 1024u,
             std::string* error_out = nullptr);

}  // namespace content::civitai::internal
