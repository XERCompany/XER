#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <absl/strings/string_view.h>

#include "cache/cache_storage.h"
#include "cache/persistent_storage.h"
#include "cache/temporary_storage.h"

namespace XER::Engine::Cache {

class CacheManager {
public:
	static CacheManager& Instance();

	const std::filesystem::path& RootDirectory() const;
	PersistentStorage& Persistent();
	const PersistentStorage& Persistent() const;
	TemporaryStorage& Temporary();
	const TemporaryStorage& Temporary() const;

	CacheEntry EntryFor(absl::string_view scope,
			    absl::string_view key,
			    absl::string_view name) const;
	std::filesystem::path DirectoryFor(absl::string_view scope,
				    absl::string_view key) const;
	std::filesystem::path PathFor(absl::string_view scope,
				       absl::string_view key,
				       absl::string_view name) const;

	bool WritePersistentText(absl::string_view scope,
				 absl::string_view key,
				 absl::string_view name,
				 const std::string& text,
				 std::string* error_out);
	bool ReadPersistentText(absl::string_view scope,
				absl::string_view key,
				absl::string_view name,
				std::string* text,
				std::string* error_out) const;
	bool WritePersistentBinary(absl::string_view scope,
				   absl::string_view key,
				   absl::string_view name,
				   const std::vector<std::uint8_t>& bytes,
				   std::string* error_out);
	bool ReadPersistentBinary(absl::string_view scope,
				  absl::string_view key,
				  absl::string_view name,
				  std::vector<std::uint8_t>* bytes,
				  std::string* error_out) const;

private:
	CacheManager();

	PersistentStorage persistent_;
	TemporaryStorage temporary_;
};

}  // namespace XER::Engine::Cache
