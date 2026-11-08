#pragma once

#include <string>
#include <string_view>

#include "flux/terminal/terminal_size_detector.h"

namespace XER::Flux::Core {

enum class LogLevel {
	kDebug = 0,
	kInfo = 1,
	kWarning = 2,
	kError = 3,
};

struct FluxConfig {
	std::string application_name = "flux";
	LogLevel log_level = LogLevel::kInfo;
	bool colors = true;
	bool timestamps = false;
	Terminal::TerminalSize terminal_size = Terminal::DefaultTerminalSize();
};

FluxConfig FluxConfigFromEnvironment();
LogLevel ParseLogLevel(std::string_view text);
const char* ToString(LogLevel level);
std::string BuildConfigSummary(const FluxConfig& config);

}  // namespace XER::Flux::Core
