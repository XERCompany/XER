#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cache/cache_entry.h"

namespace XER::Engine::Cache {

class CacheStorage {
public:
	virtual ~CacheStorage() = default;

	virtual bool Exists(const CacheEntry& entry) const = 0;
	virtual bool Remove(const CacheEntry& entry, std::string* error_out) = 0;
	virtual std::filesystem::path PathFor(const CacheEntry& entry) const = 0;
	virtual bool ReadBinary(const CacheEntry& entry,
				    std::vector<std::uint8_t>* bytes,
				    std::string* error_out) const = 0;
	virtual bool WriteBinary(const CacheEntry& entry,
				     const std::vector<std::uint8_t>& bytes,
				     std::string* error_out) = 0;

	bool ReadText(const CacheEntry& entry,
		      std::string* text,
		      std::string* error_out) const;
	bool WriteText(const CacheEntry& entry,
		       const std::string& text,
		       std::string* error_out);
};

}  // namespace XER::Engine::Cache
