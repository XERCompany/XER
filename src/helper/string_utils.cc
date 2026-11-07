#include "helper/string_utils.h"

#include <algorithm>
#include <string>

#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/strip.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef ENGINE_HAS_ICU
#define ENGINE_HAS_ICU 0
#endif

#if ENGINE_HAS_ICU
#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#endif

namespace XER::Engine::Helper::String {

std::string NormalizeUtf8(std::string_view text) {
#if ENGINE_HAS_ICU
	UErrorCode status = U_ZERO_ERROR;
	const icu::Normalizer2* normalizer = icu::Normalizer2::getNFCInstance(status);
	if (U_FAILURE(status) || normalizer == nullptr) {
		return std::string(text);
	}

	icu::UnicodeString source = icu::UnicodeString::fromUTF8(text);
	icu::UnicodeString normalized;
	normalizer->normalize(source, normalized, status);
	if (U_FAILURE(status)) {
		return std::string(text);
	}

	std::string out;
	normalized.toUTF8String(out);
	return out;
#else
	return std::string(text);
#endif
}

bool IsBlank(std::string_view text) {
	return std::all_of(text.begin(), text.end(), [](char ch) {
		return absl::ascii_isspace(static_cast<unsigned char>(ch));
	});
}

std::string TrimAsciiWhitespaceCopy(std::string_view text) {
	return std::string(absl::StripAsciiWhitespace(text));
}

std::string CanonicalizeToken(std::string_view text) {
	std::string normalized = NormalizeUtf8(TrimAsciiWhitespaceCopy(text));
	absl::AsciiStrToLower(&normalized);
	return normalized;
}

std::string DefaultString(std::string_view value, std::string_view fallback) {
	return IsBlank(value) ? std::string(fallback) : NormalizeUtf8(value);
}

std::string BuildKeyValueLine(std::string_view key, std::string_view value) {
	return absl::StrCat(NormalizeUtf8(key), "=", NormalizeUtf8(value));
}

std::string ToLowerAscii(std::string_view text) {
	std::string out(text);
	for (char& ch : out) {
		ch = absl::ascii_tolower(ch);
	}
	return out;
}

std::string ToUpperAscii(std::string_view text) {
	std::string out(text);
	for (char& ch : out) {
		ch = absl::ascii_toupper(ch);
	}
	return out;
}

bool EqualsIgnoreAsciiCase(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (absl::ascii_tolower(a[i]) != absl::ascii_tolower(b[i])) {
			return false;
		}
	}
	return true;
}

std::vector<std::string> Split(std::string_view text, char delimiter) {
	std::vector<std::string> out;
	std::size_t begin = 0;
	while (true) {
		const std::size_t pos = text.find(delimiter, begin);
		if (pos == std::string_view::npos) {
			out.emplace_back(text.substr(begin));
			break;
		}
		out.emplace_back(text.substr(begin, pos - begin));
		begin = pos + 1;
	}
	return out;
}

std::vector<std::string> SplitLines(std::string_view text) {
	if (text.empty()) {
		return {};
	}
	auto lines = Split(text, '\n');
	for (auto& line : lines) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
	}
	return lines;
}

std::string Join(const std::vector<std::string>& parts,
                 std::string_view separator) {
	std::string out;
	std::size_t total = 0;
	for (const auto& part : parts) {
		total += part.size() + separator.size();
	}
	out.reserve(total);
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i != 0) {
			out.append(separator.data(), separator.size());
		}
		out.append(parts[i]);
	}
	return out;
}

std::string ReplaceAll(std::string_view text, std::string_view from,
                       std::string_view to) {
	if (from.empty()) {
		return std::string(text);
	}
	std::string out;
	out.reserve(text.size());
	std::size_t pos = 0;
	while (true) {
		const std::size_t found = text.find(from, pos);
		if (found == std::string_view::npos) {
			out.append(text.substr(pos));
			break;
		}
		out.append(text.substr(pos, found - pos));
		out.append(to.data(), to.size());
		pos = found + from.size();
	}
	return out;
}

#ifdef _WIN32

std::wstring Utf8ToWide(std::string_view utf8) {
	if (utf8.empty()) {
		return {};
	}
	const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
	                                    utf8.data(), static_cast<int>(utf8.size()),
	                                    nullptr, 0);
	if (len <= 0) {
		return {};
	}
	std::wstring out(static_cast<std::size_t>(len), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
	                        static_cast<int>(utf8.size()), out.data(), len) !=
	    len) {
		return {};
	}
	return out;
}

std::string WideToUtf8(std::wstring_view wide) {
	if (wide.empty()) {
		return {};
	}
	const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
	                                    static_cast<int>(wide.size()), nullptr,
	                                    0, nullptr, nullptr);
	if (len <= 0) {
		return {};
	}
	std::string out(static_cast<std::size_t>(len), '\0');
	if (WideCharToMultiByte(CP_UTF8, 0, wide.data(),
	                        static_cast<int>(wide.size()), out.data(), len,
	                        nullptr, nullptr) != len) {
		return {};
	}
	return out;
}

#endif  // _WIN32

}  // namespace XER::Engine::Helper::String
