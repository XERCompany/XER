#ifndef XER_CONTENT_HEX_HEX_H
#define XER_CONTENT_HEX_HEX_H

// Single canonical home for byte<->hex conversion and hexdump formatting.
// Header-only — include "content/hex/hex.h" and link XER_Hex (INTERFACE).

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace XER::Content::Hex {
namespace detail {

inline char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline int Nibble(char c) {
    c = Lower(c);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

inline bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

} // namespace detail

// ── Encode ─────────────────────────────────────────────────────────────────

inline std::string Encode(std::string_view data, bool upper = false) {
    static const char* kLower = "0123456789abcdef";
    static const char* kUpper = "0123456789ABCDEF";
    const char* digits = upper ? kUpper : kLower;
    std::string out;
    out.reserve(data.size() * 2);
    for (const unsigned char c : data) {
        out += digits[c >> 4];
        out += digits[c & 0x0F];
    }
    return out;
}

inline std::string Encode(const void* data, std::size_t size,
                          bool upper = false) {
    return Encode(std::string_view(static_cast<const char*>(data), size),
                  upper);
}

inline std::string EncodeUpper(std::string_view data) {
    return Encode(data, true);
}

// ── Decode ─────────────────────────────────────────────────────────────────
// Tolerates ASCII whitespace between digits and mixed case. Fails on an odd
// digit count or any non-hex character (after whitespace stripping).

inline bool Decode(std::string_view hex, std::string* out,
                   std::string* error = nullptr) {
    if (out == nullptr) {
        if (error != nullptr) *error = "hex decode target is null";
        return false;
    }
    std::string compact;
    compact.reserve(hex.size());
    for (const char c : hex) {
        if (!detail::IsSpace(c)) compact += c;
    }
    // Allow an optional leading "0x"/"0X" on the whole input.
    if (compact.size() >= 2 && compact[0] == '0' &&
        (compact[1] == 'x' || compact[1] == 'X')) {
        compact.erase(0, 2);
    }
    if (compact.size() % 2 != 0) {
        if (error != nullptr) {
            *error = "hex input must contain an even number of digits: " +
                     std::to_string(compact.size());
        }
        return false;
    }
    std::string bytes;
    bytes.reserve(compact.size() / 2);
    for (std::size_t i = 0; i < compact.size(); i += 2) {
        const int hi = detail::Nibble(compact[i]);
        const int lo = detail::Nibble(compact[i + 1]);
        if (hi < 0 || lo < 0) {
            if (error != nullptr) {
                *error = "invalid hex digit at index " + std::to_string(i);
            }
            return false;
        }
        bytes += static_cast<char>((hi << 4) | lo);
    }
    *out = std::move(bytes);
    if (error != nullptr) error->clear();
    return true;
}

// ── Dump ───────────────────────────────────────────────────────────────────
// "00000000  48 65 6c 6c 6f 20 77 6f 72 6c 64  |Hello world|" style output.

inline bool IsPrintable(unsigned char c) { return c >= 32 && c <= 126; }

inline std::string Dump(std::string_view data, int bytes_per_line = 16) {
    if (bytes_per_line <= 0) bytes_per_line = 16;
    std::string out;
    char tmp[16];
    const std::size_t bpl = static_cast<std::size_t>(bytes_per_line);
    for (std::size_t base = 0; base < data.size(); base += bpl) {
        const std::size_t n = std::min(data.size() - base, bpl);
        std::snprintf(tmp, sizeof(tmp), "%08zx  ", base);
        out += tmp;
        for (std::size_t i = 0; i < n; ++i) {
            std::snprintf(tmp, sizeof(tmp), "%02x ",
                          static_cast<unsigned char>(data[base + i]));
            out += tmp;
        }
        // Pad the hex area so the ascii column aligns.
        for (std::size_t i = n; i < bpl; ++i) out += "   ";
        out += " |";
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char c =
                static_cast<unsigned char>(data[base + i]);
            out += IsPrintable(c) ? static_cast<char>(c) : '.';
        }
        out += "|\n";
    }
    return out;
}

inline std::string Dump(const void* data, std::size_t size,
                        int bytes_per_line = 16) {
    return Dump(std::string_view(static_cast<const char*>(data), size),
                bytes_per_line);
}

} // namespace XER::Content::Hex

#endif // XER_CONTENT_HEX_HEX_H
