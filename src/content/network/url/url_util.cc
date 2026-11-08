/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/url/url_util.h"

#include <cctype>
#include <sstream>
#include <vector>

namespace XER::Network::Url {

static bool IsUnreserved(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

std::string PercentEncode(const std::string& input) {
    std::string out;
    out.reserve(input.size() * 3);
    for (unsigned char c : input) {
        if (IsUnreserved(static_cast<char>(c))) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            char hex[3];
            std::snprintf(hex, sizeof(hex), "%02X", c);
            out += hex;
        }
    }
    return out;
}

std::string PercentDecode(const std::string& input) {
    std::string out;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size() &&
            std::isxdigit(static_cast<unsigned char>(input[i+1])) &&
            std::isxdigit(static_cast<unsigned char>(input[i+2]))) {
            const unsigned char c = static_cast<unsigned char>(
                std::stoul(input.substr(i+1, 2), nullptr, 16));
            out.push_back(static_cast<char>(c));
            i += 2;
        } else {
            out.push_back(input[i]);
        }
    }
    return out;
}

std::string QueryEncode(const std::string& input) {
    std::string out;
    for (unsigned char c : input) {
        if (c == ' ') { out.push_back('+'); continue; }
        if (IsUnreserved(static_cast<char>(c))) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            char hex[3];
            std::snprintf(hex, sizeof(hex), "%02X", c);
            out += hex;
        }
    }
    return out;
}

std::string NormalizePath(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg == ".") continue;
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else parts.push_back(seg);
    }
    std::string out;
    for (const auto& p : parts) { out += '/'; out += p; }
    return out.empty() ? "/" : out;
}

}  // namespace XER::Network::Url
