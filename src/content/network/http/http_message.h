/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace XER::Network::Http {

// HTTP methods supported by the router/server. kUnknown is used for
// unparsable or unsupported methods on the wire.
enum class HttpMethod {
    kGet,
    kPost,
    kPut,
    kDelete,
    kPatch,
    kHead,
    kOptions,
    kConnect,
    kTrace,
    kUnknown
};

HttpMethod HttpMethodFromString(std::string_view method);
std::string_view HttpMethodToString(HttpMethod method);

// Case-insensitive multi-map for HTTP headers. Keys are stored
// lowercased; lookups are case-insensitive.
class HttpHeaders {
public:
    void Set(const std::string& name, const std::string& value);
    void Add(const std::string& name, const std::string& value);
    std::string Get(std::string_view name) const;
    std::vector<std::string> GetAll(std::string_view name) const;
    bool Contains(std::string_view name) const;
    void Remove(std::string_view name);

    using Map = std::multimap<std::string, std::string>;
    const Map& MapView() const { return map_; }

private:
    static std::string Normalize(std::string_view name);
    Map map_;
};

// Parsed HTTP request (request line + headers + body).
struct HttpRequest {
    HttpMethod method = HttpMethod::kUnknown;
    std::string method_string;
    std::string path;    // URL-decoded path component of the target
    std::string query;   // raw query string after '?'
    std::string version = "HTTP/1.1";
    HttpHeaders headers;
    std::string body;    // binary-safe
    std::string remote;  // "ip:port" of the peer, when known

    bool IsKeepAlive() const;
    // First value of a query parameter, URL-decoded. Empty when absent.
    std::string QueryParam(std::string_view name) const;
    // All query parameters in wire order, values URL-decoded.
    std::vector<std::pair<std::string, std::string>> QueryParams() const;
};

// HTTP response. Body is binary-safe; headers are sent as stored
// (Content-Length/Connection are managed by the server writer).
struct HttpResponse {
    int status = 200;
    HttpHeaders headers;
    std::string body;

    static HttpResponse Make(int status, std::string body,
                             std::string content_type = "text/plain; charset=utf-8");
    static HttpResponse Text(std::string body, int status = 200);
    static HttpResponse Html(std::string body, int status = 200);
    static HttpResponse Json(std::string json_body, int status = 200);
    static HttpResponse JsonError(int status, const std::string& message);
    static HttpResponse Redirect(const std::string& location, int status = 302);
    static HttpResponse NotFound(const std::string& what = "not found");

    std::string StatusText() const;
};

// ---- URL helpers -------------------------------------------------------

std::string UrlDecode(std::string_view text);
std::string UrlEncode(std::string_view text);
std::vector<std::pair<std::string, std::string>> ParseQuery(
    std::string_view query);

// ---- Multipart ---------------------------------------------------------

struct MultipartPart {
    std::string name;         // form field name
    std::string filename;     // uploaded file name (may be empty)
    std::string content_type; // part Content-Type header
    std::string data;         // binary-safe part body
};

// Parses a multipart/form-data body. `content_type` is the request's
// Content-Type header value (must carry boundary=...). Returns false on
// malformed input.
bool ParseMultipartFormData(const std::string& content_type,
                            const std::string& body,
                            std::vector<MultipartPart>* parts);

}  // namespace XER::Network::Http
