#pragma once

#include <filesystem>
#include <string>

namespace XER::Cache {

struct CacheConfiguration {
	std::filesystem::path directory;
	bool readonly = false;
	bool clean = false;
};

CacheConfiguration CacheConfigurationFromEnvironment();
bool PrepareCacheDirectory(const CacheConfiguration& config, std::string* error_out);

}  // namespace XER::Cache
