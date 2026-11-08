#pragma once

#include <string>

#include "engine_params.h"

namespace XER::RuntimeSafety {

struct SandboxSettings {
	bool enabled = false;
	bool strict_require = false;
	bool allow_remote_require = false;
	bool allow_dynamic_modules = true;
};

SandboxSettings ResolveSandboxSettings(const EngineParams& params);
std::string BuildSandboxSummary(const SandboxSettings& settings);

}  // namespace XER::RuntimeSafety
