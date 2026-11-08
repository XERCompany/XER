#include "watch/watch_lock_recovery.h"

#include "cache/cache_scan_and_lock.h"

bool WatchLockRecovery::CleanupStaleLockForScript(const std::filesystem::path& script_path,
                                                  std::string* error_message) {
	return XER::Cache::CleanupScriptLock(script_path, error_message);
}
