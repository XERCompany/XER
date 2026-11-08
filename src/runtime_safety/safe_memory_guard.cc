#include "runtime_safety/safe_memory_guard.h"

#include <string>

#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/any_of.hpp>

namespace XER::RuntimeSafety {

MemoryGuardSnapshot CaptureMemoryGuardSnapshot(const EngineParams& params) {
	MemoryGuardSnapshot snapshot;
	snapshot.hard_limit_mib = params.memory_hard_limit_mib > 0
		? params.memory_hard_limit_mib
		: params.max_memory_used;
	snapshot.warn_limit_mib = params.memory_warn_mib;
	snapshot.timeout_seconds = params.timeout_seconds;
	snapshot.check_interval_ms = params.memory_check_interval_ms;
	return snapshot;
}

bool ValidateMemoryGuardSnapshot(const MemoryGuardSnapshot& snapshot, std::string* error_out) {
	if (snapshot.hard_limit_mib > 0 && snapshot.warn_limit_mib > 0
		&& snapshot.warn_limit_mib >= snapshot.hard_limit_mib) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat(
				"memory_warn_mib must be lower than hard memory limit: warn=",
				snapshot.warn_limit_mib,
				", hard=",
				snapshot.hard_limit_mib);
		}
		return false;
	}
	if (snapshot.check_interval_ms <= 0) {
		if (error_out != nullptr) {
			*error_out = "memory_check_interval_ms must be positive";
		}
		return false;
	}
	return true;
}

std::string BuildMemoryGuardSummary(const MemoryGuardSnapshot& snapshot) {
	return absl::StrCat(
		"memory_guard(hard_mib=", snapshot.hard_limit_mib,
		", warn_mib=", snapshot.warn_limit_mib,
		", timeout_s=", snapshot.timeout_seconds,
		", interval_ms=", snapshot.check_interval_ms,
		")");
}

}  // namespace XER::RuntimeSafety
