#include "runtime_safety/safe_sandbox.h"

#include <string>

#include <absl/strings/str_cat.h>

namespace XER::RuntimeSafety {

SandboxSettings ResolveSandboxSettings(const EngineParams& params) {
	SandboxSettings settings;
	settings.enabled = params.sandbox;
	settings.strict_require = params.strict_require;
	settings.allow_remote_require = !params.sandbox && params.allow_remote_require;
	settings.allow_dynamic_modules = !params.sandbox;
	return settings;
}

std::string BuildSandboxSummary(const SandboxSettings& settings) {
	return absl::StrCat(
		"sandbox(enabled=", settings.enabled ? "true" : "false",
		", strict_require=", settings.strict_require ? "true" : "false",
		", remote_require=", settings.allow_remote_require ? "true" : "false",
		", dynamic_modules=", settings.allow_dynamic_modules ? "true" : "false",
		")");
}

}  // namespace XER::RuntimeSafety
