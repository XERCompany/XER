/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace XER::Network::Http {

// Trim ASCII whitespace (space, tab, \r, \n) from both ends of string.
// @param s     Input string
// @return      Trimmed string (or empty if only whitespace)
std::string TrimWhitespace(const std::string& s);

// Case-insensitive ASCII comparison (ignores case for ASCII letters A-Z, a-z).
// @param a     First string to compare
// @param b     Second string to compare
// @return      True if equal (ignoring case), false otherwise
bool IEqualASCII(const std::string& a, const std::string& b);

// Split a header field value by delimiter (e.g., comma for Content-Type params).
// Each token is trimmed of leading/trailing whitespace.
// @param value     Header value to split
// @param delimiter Character to split on (default: comma)
// @return          Vector of trimmed tokens
std::vector<std::string> SplitHeaderValue(const std::string& value,
                                           char delimiter = ',');

// Format a byte count as a Content-Length header value.
// @param n     Number of bytes
// @return      Formatted string representation
std::string FormatContentLength(size_t n);

// Check if an HTTP method is "safe" (no side effects, read-only).
// Safe methods: GET, HEAD, OPTIONS
// @param method   HTTP method name (e.g., "GET", "POST")
// @return         True if method is safe
bool IsMethodSafe(const std::string& method);

// Check if an HTTP method is idempotent (same effect when repeated).
// Idempotent methods: GET, HEAD, OPTIONS, PUT, DELETE
// @param method   HTTP method name
// @return         True if method is idempotent
bool IsMethodIdempotent(const std::string& method);

// Get the standard HTTP Reason-Phrase for a status code.
// @param status_code   HTTP status code (e.g., 200, 404, 500)
// @return              Reason phrase (e.g., "OK", "Not Found", "Unknown")

}  // namespace XER::Network::Http
