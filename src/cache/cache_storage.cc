#include "cache/cache_storage.h"

#include <cstdint>
#include <string>
#include <vector>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

namespace XER::Cache {

bool CacheStorage::ReadText(const CacheEntry& entry,
			    std::string* text,
			    std::string* error_out) const {
	if (text == nullptr) {
		if (error_out != nullptr) {
			*error_out = "Cache text target is null";
		}
		return false;
	}

	std::vector<std::uint8_t> bytes;
	if (!ReadBinary(entry, &bytes, error_out)) {
		return false;
	}
	*text = bytes
		| ranges::views::transform([](std::uint8_t byte) {
			return static_cast<char>(byte);
		})
		| ranges::to<std::string>();
	return true;
}

bool CacheStorage::WriteText(const CacheEntry& entry,
			     const std::string& text,
			     std::string* error_out) {
	const auto bytes = text
		| ranges::views::transform([](char ch) {
			return static_cast<std::uint8_t>(ch);
		})
		| ranges::to<std::vector<std::uint8_t>>();
	return WriteBinary(entry, bytes, error_out);
}

}  // namespace XER::Cache
