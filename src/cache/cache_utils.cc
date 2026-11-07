#include "cache/cache_utils.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>

#include <openssl/sha.h>

#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

#include "helper/string_utils.h"

namespace XER::Engine::Cache {
namespace {

std::string HexDigest(const unsigned char* digest, std::size_t size) {
	static constexpr char kHex[] = "0123456789abcdef";
	std::string out;
	out.resize(size * 2);
	for (std::size_t index = 0; index < size; ++index) {
		out[index * 2] = kHex[(digest[index] >> 4) & 0x0F];
		out[index * 2 + 1] = kHex[digest[index] & 0x0F];
	}
	return out;
}

}  // namespace

std::string HashKey(absl::string_view text) {
	const std::string normalized = XER::Engine::Helper::String::NormalizeUtf8(text);
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256(reinterpret_cast<const unsigned char*>(normalized.data()), normalized.size(), digest);
	return HexDigest(digest, SHA256_DIGEST_LENGTH);
}

std::string SanitizeFileName(absl::string_view text) {
	const std::string normalized = XER::Engine::Helper::String::NormalizeUtf8(text);
	const auto sanitized_bytes = normalized
		| ranges::views::transform([](char ch) {
			const unsigned char byte = static_cast<unsigned char>(ch);
			return static_cast<char>(std::isalnum(byte) || ch == '.' || ch == '_' || ch == '-'
				? ch
				: '_');
		})
		| ranges::to<std::string>();
	return sanitized_bytes.empty() ? std::string("entry") : sanitized_bytes;
}

std::filesystem::path BuildScopedDirectory(const std::filesystem::path& root,
					   absl::string_view scope,
					   absl::string_view key) {
	return root / SanitizeFileName(XER::Engine::Helper::String::CanonicalizeToken(scope)) / HashKey(key);
}

std::filesystem::path TempPathForTarget(const std::filesystem::path& target,
					absl::string_view extension_tag) {
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
	std::filesystem::path temp_path = target;
	temp_path += absl::StrCat(".tmp.", SanitizeFileName(extension_tag), ".", ticks);
	return temp_path;
}

bool CommitTempFile(const std::filesystem::path& tmp,
		    const std::filesystem::path& target,
		    std::string* error_out) {
	std::error_code error;
	std::filesystem::rename(tmp, target, error);
	if (!error) {
		return true;
	}

	std::filesystem::remove(target, error);
	error.clear();
	std::filesystem::rename(tmp, target, error);
	if (!error) {
		return true;
	}

	if (error_out != nullptr) {
		*error_out = absl::StrCat("Failed to commit cache file: ", target.string(), ", reason: ", error.message());
	}
	std::filesystem::remove(tmp, error);
	return false;
}

}  // namespace XER::Engine::Cache
