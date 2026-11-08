/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_util.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace XER::Network::Http {

std::string TrimWhitespace(const std::string& s) {
    return XER::Helper::String::TrimAsciiWhitespaceCopy(s);
}

bool IEqualASCII(const std::string& a, const std::string& b) {
    return XER::Helper::String::EqualsIgnoreAsciiCase(a, b);
}

std::vector<std::string> SplitHeaderValue(const std::string& value, char delimiter) {
    std::vector<std::string> out;
    std::istringstream ss(value);
    std::string tok;
    while (std::getline(ss, tok, delimiter))
        out.push_back(TrimWhitespace(tok));
    return out;
}

std::string FormatContentLength(size_t n) { return std::to_string(n); }

bool IsMethodSafe(const std::string& method) {
    return method == "GET" || method == "HEAD" || method == "OPTIONS";
}

bool IsMethodIdempotent(const std::string& method) {
    return IsMethodSafe(method) || method == "PUT" || method == "DELETE";
}

std::string ReasonPhrase(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

}  // namespace XER::Network::Http
