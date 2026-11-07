#include "cache/cache_entry.h"

#include <array>

#include <range/v3/algorithm/all_of.hpp>

#include "helper/string_utils.h"

namespace XER::Engine::Cache {

CacheEntry MakeEntry(absl::string_view scope,
		     absl::string_view key,
		     absl::string_view name) {
	return CacheEntry{
		XER::Engine::Helper::String::CanonicalizeToken(scope),
		XER::Engine::Helper::String::NormalizeUtf8(key),
		XER::Engine::Helper::String::NormalizeUtf8(name),
		/*ttl_seconds=*/0,
	};
}

CacheEntry MakeEntryWithTtl(absl::string_view scope,
			    absl::string_view key,
			    absl::string_view name,
			    std::uint32_t ttl_seconds) {
	CacheEntry entry = MakeEntry(scope, key, name);
	entry.ttl_seconds = ttl_seconds;
	return entry;
}

bool IsValid(const CacheEntry& entry) {
	return ranges::all_of(std::array<std::string, 3>{entry.scope, entry.key, entry.name},
		[](const std::string& part) {
			return !XER::Engine::Helper::String::IsBlank(part);
		});
}

}  // namespace XER::Engine::Cache
