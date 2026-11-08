#pragma once

#include <filesystem>
#include <string>

#include <absl/strings/string_view.h>

namespace XER::Cache {

std::string HashKey(absl::string_view text);
std::string SanitizeFileName(absl::string_view text);
std::filesystem::path BuildScopedDirectory(const std::filesystem::path& root,
					   absl::string_view scope,
					   absl::string_view key);
std::filesystem::path TempPathForTarget(const std::filesystem::path& target,
					absl::string_view extension_tag);
bool CommitTempFile(const std::filesystem::path& tmp,
		    const std::filesystem::path& target,
		    std::string* error_out);

}  // namespace XER::Cache
