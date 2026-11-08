/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace XER::Network::Url {

// HTTP response received from a network request.
struct URLResponse {
    // HTTP status code (e.g., 200, 404, 500)
    // Set to 0 if request failed before receiving response
    int         status_code = 0;
    
    // HTTP reason phrase (e.g., "OK", "Not Found")
    std::string status_text;
    
    // Response headers as key-value pairs (case-insensitive keys)
    std::map<std::string, std::string> headers;
    
    // Response body as raw bytes
    std::vector<uint8_t> body;
};

// Callback invoked when a URL request completes.
// @param ok        True if request succeeded (regardless of HTTP status)
// @param response  Response object (valid only if ok=true)
// @param error     Error message (valid only if ok=false)
using URLResponseCallback = std::function<void(bool ok,
                                               const URLResponse& response,
                                               const std::string& error)>;

// HTTP request configuration for URLRequest operations.
struct URLRequest {
    // HTTP method (default: "GET")
    std::string method  = "GET";
    
    // Target URL (e.g., "http://example.com/path?query=value")
    std::string url;
    
    // Request headers as key-value pairs (e.g., "Content-Type": "application/json")
    std::map<std::string, std::string> headers;
    
    // Request body as raw bytes (empty for GET)
    std::vector<uint8_t> body;
    
    // Operation timeout in milliseconds (default: 30 seconds)
    uint32_t timeout_ms = 30000;
    
    // Whether to automatically follow HTTP redirects (3xx)
    bool     follow_redirects = true;
    
    // Maximum number of redirects to follow (prevents infinite loops)
    int      max_redirects    = 5;
};

}  // namespace XER::Network::Url
