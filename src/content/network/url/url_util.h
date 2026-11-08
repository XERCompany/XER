/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>

namespace XER::Network::Url {

// Percent-encode all reserved characters in |input|.
std::string PercentEncode(const std::string& input);

// Decode percent-encoded characters.
std::string PercentDecode(const std::string& input);

// Encodes only query-string unsafe chars (space → +).
std::string QueryEncode(const std::string& input);

// Normalise a URL path (resolve . and ..).
std::string NormalizePath(const std::string& path);

}  // namespace XER::Network::Url
