#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <absl/strings/string_view.h>

#include "cache/cache_storage.h"

namespace XER::Cache {

class PersistentStorage final : public CacheStorage {
public:
	explicit PersistentStorage(std::filesystem::path root);

	const std::filesystem::path& root() const;
	bool EnsureRootDirectory(std::string* error_out) const;
	std::filesystem::path DirectoryFor(absl::string_view scope,
					  absl::string_view key) const;

	bool Exists(const CacheEntry& entry) const override;
	bool Remove(const CacheEntry& entry, std::string* error_out) override;
	std::filesystem::path PathFor(const CacheEntry& entry) const override;
	bool ReadBinary(const CacheEntry& entry,
				    std::vector<std::uint8_t>* bytes,
				    std::string* error_out) const override;
	bool WriteBinary(const CacheEntry& entry,
				     const std::vector<std::uint8_t>& bytes,
				     std::string* error_out) override;

	bool ExistsRelative(const std::filesystem::path& relative_path) const;
	bool RemoveRelative(const std::filesystem::path& relative_path,
			 std::string* error_out);
	bool ReadBinaryRelative(const std::filesystem::path& relative_path,
				  std::vector<std::uint8_t>* bytes,
				  std::string* error_out) const;
	bool WriteBinaryRelative(const std::filesystem::path& relative_path,
				   const std::vector<std::uint8_t>& bytes,
				   std::string* error_out);
	bool ReadTextRelative(const std::filesystem::path& relative_path,
				 std::string* text,
				 std::string* error_out) const;
	bool WriteTextRelative(const std::filesystem::path& relative_path,
				  const std::string& text,
				  std::string* error_out);

private:
	std::filesystem::path ResolveRelative(const std::filesystem::path& relative_path) const;

	std::filesystem::path root_;
};

}  // namespace XER::Cache
