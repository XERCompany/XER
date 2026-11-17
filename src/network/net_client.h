/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

// Small facade over XER::Network::Http::SyncRequest for callers that
// just need "GET this URL / POST this body / save this file" without
// touching the transport layer. Deliberately narrow: no sessions, no
// streaming, no callbacks — one blocking call per request.
//
// `ok` on NetResponse mirrors transport_ok from the sync path: the
// request reached the server and a response came back. HTTP error
// statuses (404, 500, ...) still report ok == true — inspect `status`.

#include <map>
#include <string>
#include <vector>

namespace XER::Network {

struct NetResponse {
    long status = 0;                             // HTTP status, 0 on transport error
    std::string body;                            // response body (binary-safe)
    std::map<std::string, std::string> headers;  // response headers, lowercase names
    std::string error;                           // failure text with request context
    bool ok = false;                             // transport completed (any HTTP status)
};

// Performs one blocking request. `headers` entries are "Name: value"
// lines sent verbatim. Rejects an empty url/method and a non-positive
// timeout before touching the transport; `method` is upper-cased so
// "get"/"Get" behave like "GET".
NetResponse Fetch(const std::string& method, const std::string& url,
                  const std::string& body = {},
                  const std::vector<std::string>& headers = {},
                  long timeout_ms = 60000);

NetResponse Get(const std::string& url, long timeout_ms = 60000);

// Sends `body` verbatim; `content_type` becomes a Content-Type header
// (empty content_type sends none).
NetResponse Post(const std::string& url, const std::string& body,
                 const std::string& content_type = {},
                 long timeout_ms = 60000);

// GETs `url` and writes the body to `dest_path` (binary, truncates).
// Fails on transport error, on HTTP status >= 400 (so an error page is
// never mistaken for the payload), or when the file cannot be opened —
// `error_out` receives a message naming the url/path involved.
bool DownloadFile(const std::string& url, const std::string& dest_path,
                  long timeout_ms = 120000,
                  std::string* error_out = nullptr);

}  // namespace XER::Network
