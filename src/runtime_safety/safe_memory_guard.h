#pragma once

#include <string>

#include "engine_params.h"

namespace XER::RuntimeSafety {

struct MemoryGuardSnapshot {
	int hard_limit_mib = -1;
	int warn_limit_mib = -1;
	int timeout_seconds = -1;
	int check_interval_ms = 250;
};

MemoryGuardSnapshot CaptureMemoryGuardSnapshot(const EngineParams& params);
bool ValidateMemoryGuardSnapshot(const MemoryGuardSnapshot& snapshot, std::string* error_out);
std::string BuildMemoryGuardSummary(const MemoryGuardSnapshot& snapshot);

}  // namespace XER::RuntimeSafety
