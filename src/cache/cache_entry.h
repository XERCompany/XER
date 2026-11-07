#pragma once

#include <cstdint>
#include <string>

#include <absl/strings/string_view.h>

namespace XER::Engine::Cache {

struct CacheEntry {
	std::string scope;
	std::string key;
	std::string name;
	// TTL in seconds from the moment of writing. 0 means no expiry.
	std::uint32_t ttl_seconds = 0;
};

CacheEntry MakeEntry(absl::string_view scope,
		     absl::string_view key,
		     absl::string_view name);
// Overload that sets a TTL on the entry.
CacheEntry MakeEntryWithTtl(absl::string_view scope,
			    absl::string_view key,
			    absl::string_view name,
			    std::uint32_t ttl_seconds);
bool IsValid(const CacheEntry& entry);

}  // namespace XER::Engine::Cache
