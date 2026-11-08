#pragma once

#include <string>
#include <string_view>
#include <vector>


namespace XER::Helper::String {

std::string NormalizeUtf8(std::string_view text);

bool IsBlank(std::string_view text);

std::string TrimAsciiWhitespaceCopy(std::string_view text);

std::string CanonicalizeToken(std::string_view text);

std::string DefaultString(std::string_view value, std::string_view fallback);

std::string BuildKeyValueLine(std::string_view key, std::string_view value);

// ASCII-only case conversion: touches A-Z/a-z, leaves UTF-8 multibyte
// sequences untouched (locale-independent, allocation-free input view).
std::string ToLowerAscii(std::string_view text);
std::string ToUpperAscii(std::string_view text);

// Case-insensitive ASCII equality (A-Z == a-z, everything else exact).
bool EqualsIgnoreAsciiCase(std::string_view a, std::string_view b);

// Prefix / suffix tests. Concrete char overloads accept std::string /
// std::string_view / const char* via implicit conversion; the template
// covers wchar_t views.
inline bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

inline bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

inline bool StartsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

inline bool EndsWith(std::wstring_view text, std::wstring_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

template <typename CharT>
bool StartsWith(std::basic_string_view<CharT> text,
                std::basic_string_view<CharT> prefix) {
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

template <typename CharT>
bool EndsWith(std::basic_string_view<CharT> text,
              std::basic_string_view<CharT> suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

// Split on `delimiter`, keeping empty fields (predictable positions).
// For line splitting use SplitLines(text) or Split(text, '\n').
std::vector<std::string> Split(std::string_view text, char delimiter);

// Split into lines on '\n', stripping one trailing '\r' per line (CRLF-safe).
// Empty input yields an empty vector; empty lines are preserved.
std::vector<std::string> SplitLines(std::string_view text);

// Join `parts` with `separator` between elements (no trailing separator).
std::string Join(const std::vector<std::string>& parts,
                 std::string_view separator);

// Replace every occurrence of `from` with `to` (empty `from` is a no-op).
std::string ReplaceAll(std::string_view text, std::string_view from,
                       std::string_view to);

#ifdef _WIN32
// UTF-8 <-> UTF-16 conversions. Best-effort: invalid input yields an
// empty result rather than throwing (UI text paths tolerate loss).
std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);
#endif

}  // namespace XER::Helper::String
