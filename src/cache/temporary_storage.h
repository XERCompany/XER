#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cache/cache_storage.h"

namespace XER::Cache {

class TemporaryStorage final : public CacheStorage {
public:
	bool Exists(const CacheEntry& entry) const override;
	bool Remove(const CacheEntry& entry, std::string* error_out) override;
	std::filesystem::path PathFor(const CacheEntry& entry) const override;
	bool ReadBinary(const CacheEntry& entry,
				    std::vector<std::uint8_t>* bytes,
				    std::string* error_out) const override;
	bool WriteBinary(const CacheEntry& entry,
				     const std::vector<std::uint8_t>& bytes,
				     std::string* error_out) override;

private:
	static std::string StorageKey(const CacheEntry& entry);

	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::vector<std::uint8_t>> entries_;
};

}  // namespace XER::Cache
