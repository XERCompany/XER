#include "flux/core/flux_config.h"

#include <string>

#include <absl/strings/str_cat.h>

#include "helper/env_utils.h"
#include "helper/string_utils.h"

namespace XER::Flux::Core {
namespace {

std::string EnvString(const char* key, const std::string& fallback = {}) {
	const std::string raw = XER::Helper::GetEnv(key);
	if (raw.empty()) {
		return fallback;
	}
	return raw;
}

absl::string_view AsAbsl(std::string_view text) {
	return absl::string_view(text.data(), text.size());
}

bool EnvBool(const char* key) {
	const std::string raw = XER::Helper::String::CanonicalizeToken(EnvString(key));
	return raw == "1" || raw == "true" || raw == "on" || raw == "yes";
}

std::size_t EnvPositiveSize(const char* key, std::size_t fallback) {
	const std::string raw = EnvString(key);
	if (raw.empty()) {
		return fallback;
	}
	char* end = nullptr;
	const unsigned long parsed = std::strtoul(raw.c_str(), &end, 10);
	if (end == raw.c_str() || parsed == 0UL) {
		return fallback;
	}
	return static_cast<std::size_t>(parsed);
}

}  // namespace

FluxConfig FluxConfigFromEnvironment() {
	FluxConfig config;
	config.application_name = XER::Helper::String::DefaultString(
		EnvString("FLUX_APP_NAME", EnvString("ENGINE_APP_NAME", "flux")),
		"flux");
	config.log_level = ParseLogLevel(EnvString("FLUX_LOG_LEVEL", EnvString("ENGINE_LOG_LEVEL", "info")));
	config.colors = !EnvBool("FLUX_NO_COLOR") && !EnvBool("ENGINE_STACK_FORMATING_NO_COLOR");
	config.timestamps = EnvBool("FLUX_TIMESTAMPS") || EnvBool("ENGINE_TIMESTAMPS");
	config.terminal_size = Terminal::DetectTerminalSize();
	config.terminal_size.columns = EnvPositiveSize("FLUX_COLUMNS", config.terminal_size.columns);
	config.terminal_size.rows = EnvPositiveSize("FLUX_ROWS", config.terminal_size.rows);
	config.terminal_size = Terminal::ClampTerminalSize(config.terminal_size, 20, 5);
	return config;
}

LogLevel ParseLogLevel(std::string_view text) {
	const std::string token = XER::Helper::String::CanonicalizeToken(AsAbsl(text));
	if (token == "debug") {
		return LogLevel::kDebug;
	}
	if (token == "warn" || token == "warning") {
		return LogLevel::kWarning;
	}
	if (token == "error") {
		return LogLevel::kError;
	}
	return LogLevel::kInfo;
}

const char* ToString(LogLevel level) {
	switch (level) {
	case LogLevel::kDebug:
		return "DEBUG";
	case LogLevel::kInfo:
		return "INFO";
	case LogLevel::kWarning:
		return "WARN";
	case LogLevel::kError:
		return "ERROR";
	}
	return "INFO";
}

std::string BuildConfigSummary(const FluxConfig& config) {
	return absl::StrCat(
		"app=", config.application_name,
		", log_level=", ToString(config.log_level),
		", colors=", config.colors ? "true" : "false",
		", timestamps=", config.timestamps ? "true" : "false",
		", size=", config.terminal_size.columns, "x", config.terminal_size.rows,
		", interactive=", config.terminal_size.interactive ? "true" : "false");
}

}  // namespace XER::Flux::Core
