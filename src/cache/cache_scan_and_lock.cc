#include "cache/cache_scan_and_lock.h"

#include <filesystem>
#include <string>
#include <thread>

#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/any_of.hpp>

namespace XER::Cache {

std::filesystem::path ScriptLockPath(const std::filesystem::path& script_path) {
	return script_path.parent_path()
		/ absl::StrCat(script_path.filename().string(), ".flowcache.lock");
}

bool AcquireDirectoryLock(const std::filesystem::path& lock_path,
			  const LockOptions& options,
			  std::string* error_out) {
	const int max_attempts = options.max_attempts > 0 ? options.max_attempts : 1;
	for (int attempt = 0; attempt < max_attempts; ++attempt) {
		std::error_code error;
		if (std::filesystem::create_directory(lock_path, error)) {
			return true;
		}
		if (error && error_out != nullptr) {
			*error_out = absl::StrCat("Failed to create lock: ", lock_path.string(), ", reason: ", error.message());
			return false;
		}
		std::this_thread::sleep_for(options.retry_delay);
	}

	if (error_out != nullptr) {
		*error_out = absl::StrCat("Timed out waiting for lock: ", lock_path.string());
	}
	return false;
}

bool CleanupDirectoryLock(const std::filesystem::path& lock_path,
			  std::string* error_out) {
	std::error_code error;
	if (!std::filesystem::exists(lock_path, error)) {
		return true;
	}
	if (error) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to inspect lock: ", lock_path.string(), ", reason: ", error.message());
		}
		return false;
	}

	error.clear();
	std::filesystem::remove_all(lock_path, error);
	if (!error) {
		return true;
	}
	if (error_out != nullptr) {
		*error_out = absl::StrCat("Failed to remove stale lock: ", lock_path.string(), ", reason: ", error.message());
	}
	return false;
}

bool CleanupScriptLock(const std::filesystem::path& script_path,
		       std::string* error_out) {
	return CleanupDirectoryLock(ScriptLockPath(script_path), error_out);
}

}  // namespace XER::Cache
