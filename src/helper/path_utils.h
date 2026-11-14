#pragma once

// UTF-8 path helpers.
//
// XER APIs carry filesystem paths as std::string in UTF-8. On Windows
// std::filesystem::path stores UTF-16 natively and constructs from char
// bytes via the ANSI codepage, while std::ifstream(const char*) and
// FFmpeg's avformat_open_input() both disagree on the expected encoding
// (ANSI vs UTF-8). These helpers convert at the boundary so a UTF-8
// std::string round-trips correctly through std::filesystem and file I/O.
//
// On POSIX the native path encoding is already UTF-8 on supported
// platforms, so the helpers are pass-throughs.

#include <string>
#include <string_view>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace XER::Helper::Path {

#if defined(_WIN32)
namespace detail {

inline bool WideFromBytes(const std::string& bytes, UINT codepage,
                          std::wstring& out) {
    if (bytes.empty()) {
        out.clear();
        return true;
    }
    const int flags = (codepage == CP_UTF8) ? MB_ERR_INVALID_CHARS : 0;
    const int n = MultiByteToWideChar(codepage, flags, bytes.data(),
                                      static_cast<int>(bytes.size()),
                                      nullptr, 0);
    if (n <= 0) return false;
    out.resize(static_cast<std::size_t>(n));
    return MultiByteToWideChar(codepage, flags, bytes.data(),
                               static_cast<int>(bytes.size()), out.data(), n) == n;
}

inline std::string BytesFromWide(const std::wstring& wide, UINT codepage) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(codepage, 0, wide.data(),
                                      static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(codepage, 0, wide.data(),
                        static_cast<int>(wide.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

}  // namespace detail
#endif

// Returns `path` as UTF-8. On Windows, input that is not valid UTF-8 is
// treated as ANSI (system codepage) and converted; valid UTF-8 and ASCII
// pass through unchanged. On POSIX the bytes are returned as-is.
inline std::string NormalizeUtf8Path(std::string_view path) {
#if defined(_WIN32)
    const std::string_view sv = path;
    if (sv.empty()) return {};
    // Fast path: pure ASCII is identical in UTF-8 and ANSI.
    bool ascii_only = true;
    for (const char c : sv) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            ascii_only = false;
            break;
        }
    }
    if (ascii_only) return std::string(sv);

    std::wstring wide;
    if (detail::WideFromBytes(std::string(sv), CP_UTF8, wide)) {
        return std::string(sv);  // already valid UTF-8
    }
    // Legacy ANSI bytes (e.g. argv from char** main) -> UTF-8.
    if (detail::WideFromBytes(std::string(sv), CP_ACP, wide)) {
        return detail::BytesFromWide(wide, CP_UTF8);
    }
    return std::string(sv);  // give up; downstream will report not-found
#else
    return std::string(path);
#endif
}

// UTF-8 string -> filesystem path. On Windows the string is decoded as
// UTF-8 (falling back to ANSI for legacy callers); on POSIX the bytes are
// used directly.
inline std::filesystem::path FromUtf8(const std::string& utf8) {
#if defined(_WIN32)
    std::wstring wide;
    if (detail::WideFromBytes(utf8, CP_UTF8, wide) ||
        detail::WideFromBytes(utf8, CP_ACP, wide)) {
        return std::filesystem::path(std::move(wide));
    }
    return std::filesystem::path(utf8);  // last resort: ANSI semantics
#else
    return std::filesystem::path(utf8);
#endif
}

// Filesystem path -> UTF-8 string.
inline std::string ToUtf8(const std::filesystem::path& p) {
#if defined(_WIN32)
    return detail::BytesFromWide(p.wstring(), CP_UTF8);
#else
    return p.string();
#endif
}

}  // namespace XER::Helper::Path
