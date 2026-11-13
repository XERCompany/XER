#include "cache/cache_configuration.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/any_of.hpp>

#include "helper/env_utils.h"
#include "helper/string_utils.h"

namespace XER::Cache {
namespace {

std::filesystem::path DefaultCacheDirectory() {
	std::error_code error;
	const std::filesystem::path temp_directory = std::filesystem::temp_directory_path(error);
	if (error || temp_directory.empty()) {
		return std::filesystem::path(".flowcache");
	}
	return temp_directory / "XER" / "cache";
}

bool EnvFlagEnabled(const char* key) {
	const std::string raw = XER::Helper::GetEnv(key);
	if (raw.empty() || !ranges::any_of(std::string_view(raw), [](char ch) {
		return !absl::ascii_isspace(static_cast<unsigned char>(ch));
	})) {
		return false;
	}

	const std::string normalized = XER::Helper::String::CanonicalizeToken(raw);
	return normalized != "0"
		&& normalized != "false"
		&& normalized != "off"
		&& normalized != "no";
}

}  // namespace

CacheConfiguration CacheConfigurationFromEnvironment() {
	CacheConfiguration config;
	const std::string raw_directory = XER::Helper::GetEnv("ENGINE_CACHE_DIR");
	if (!raw_directory.empty() && !XER::Helper::String::IsBlank(raw_directory)) {
		config.directory = XER::Helper::String::NormalizeUtf8(raw_directory);
	} else {
		config.directory = DefaultCacheDirectory();
	}
	config.readonly = EnvFlagEnabled("ENGINE_CACHE_READONLY");
	config.clean = EnvFlagEnabled("ENGINE_CACHE_CLEAN");
	return config;
}

bool PrepareCacheDirectory(const CacheConfiguration& config, std::string* error_out) {
	if (config.directory.empty()) {
		if (error_out != nullptr) {
			*error_out = "Cache directory is empty";
		}
		return false;
	}

	std::error_code error;
	if (config.clean && std::filesystem::exists(config.directory, error) && !error) {
		for (std::filesystem::directory_iterator it(config.directory, error); !error && it != std::filesystem::directory_iterator(); ++it) {
			std::filesystem::remove_all(it->path(), error);
			if (error) {
				break;
			}
		}
	}
	if (error) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to clean cache directory: ", config.directory.string());
		}
		return false;
	}

	error.clear();
	std::filesystem::create_directories(config.directory, error);
	if (error) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to create cache directory: ", config.directory.string());
		}
		return false;
	}
	return true;
}

}  // namespace XER::Cache
