#include "cache/temporary_storage.h"

#include <filesystem>
#include <string>

#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/contains.hpp>

#include "cache/cache_utils.h"

namespace XER::Cache {

std::string TemporaryStorage::StorageKey(const CacheEntry& entry) {
	return absl::StrCat(entry.scope, "::", entry.key, "::", entry.name);
}

bool TemporaryStorage::Exists(const CacheEntry& entry) const {
	std::lock_guard<std::mutex> lock(mutex_);
	return ranges::contains(entries_, StorageKey(entry), &std::unordered_map<std::string, std::vector<std::uint8_t>>::value_type::first);
}

bool TemporaryStorage::Remove(const CacheEntry& entry, std::string* /*error_out*/) {
	std::lock_guard<std::mutex> lock(mutex_);
	entries_.erase(StorageKey(entry));
	return true;
}

std::filesystem::path TemporaryStorage::PathFor(const CacheEntry& entry) const {
	return std::filesystem::path("memory")
		/ SanitizeFileName(entry.scope)
		/ HashKey(entry.key)
		/ SanitizeFileName(entry.name);
}

bool TemporaryStorage::ReadBinary(const CacheEntry& entry,
				 std::vector<std::uint8_t>* bytes,
				 std::string* error_out) const {
	if (bytes == nullptr) {
		if (error_out != nullptr) {
			*error_out = "Temporary cache target is null";
		}
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	const auto it = entries_.find(StorageKey(entry));
	if (it == entries_.end()) {
		if (error_out != nullptr) {
			*error_out = "Temporary cache entry not found";
		}
		return false;
	}
	*bytes = it->second;
	return true;
}

bool TemporaryStorage::WriteBinary(const CacheEntry& entry,
				  const std::vector<std::uint8_t>& bytes,
				  std::string* error_out) {
	if (!IsValid(entry)) {
		if (error_out != nullptr) {
			*error_out = "Temporary cache entry is invalid";
		}
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	entries_[StorageKey(entry)] = bytes;
	return true;
}

}  // namespace XER::Cache
