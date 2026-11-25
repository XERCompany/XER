/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — .xersync dependency-manifest dev utility.
 * Small shared helpers: string utils, ISO-8601 timestamps, fs alias.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace xer_sync_dev {

namespace fs = std::filesystem;

inline std::string Trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

inline bool StartsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

inline bool EndsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

inline std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Splits `s` on any of the chars in `delims`, dropping empty tokens.
inline std::vector<std::string> SplitTokens(std::string_view s,
                                            std::string_view delims) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        const size_t b = s.find_first_not_of(delims, i);
        if (b == std::string_view::npos) break;
        const size_t e = s.find_first_of(delims, b);
        out.emplace_back(s.substr(b, e == std::string_view::npos
                                       ? std::string_view::npos : e - b));
        i = (e == std::string_view::npos) ? s.size() : e + 1;
    }
    return out;
}

// Forward-slash normalized relative path (generic form for cmake emit).
inline std::string GenericPath(const fs::path& p) {
    return p.generic_string();
}

// --- ISO-8601 UTC timestamps ("YYYY-MM-DDTHH:MM:SSZ") -------------------

inline std::string NowIso8601() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// Parses "YYYY-MM-DDTHH:MM:SSZ"; returns false on malformed input.
inline bool ParseIso8601(const std::string& s, std::time_t* out) {
    if (s.size() < 19) return false;
    std::tm tm{};
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
        return false;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = 0;
#ifdef _WIN32
    const std::time_t t = _mkgmtime(&tm);
#else
    const std::time_t t = timegm(&tm);
#endif
    if (t == static_cast<std::time_t>(-1)) return false;
    if (out) *out = t;
    return true;
}

// Whole days between an ISO timestamp and now; -1 when unparseable.
inline int DaysSinceIso(const std::string& iso) {
    std::time_t then{};
    if (iso.empty() || !ParseIso8601(iso, &then)) return -1;
    const std::time_t now = std::time(nullptr);
    return static_cast<int>(std::difftime(now, then) / 86400.0);
}

}  // namespace xer_sync_dev
