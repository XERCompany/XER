#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace XER::Cache {

struct LockOptions {
	int max_attempts = 100;
	std::chrono::milliseconds retry_delay{20};
};

std::filesystem::path ScriptLockPath(const std::filesystem::path& script_path);
bool AcquireDirectoryLock(const std::filesystem::path& lock_path,
			  const LockOptions& options,
			  std::string* error_out);
bool CleanupDirectoryLock(const std::filesystem::path& lock_path,
			  std::string* error_out);
bool CleanupScriptLock(const std::filesystem::path& script_path,
		       std::string* error_out);

}  // namespace XER::Cache
