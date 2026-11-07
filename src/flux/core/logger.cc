#include "flux/core/logger.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_styles.h"

namespace XER::flux::core {
namespace {

std::string BuildTimestamp() {
	const std::time_t now = std::time(nullptr);
	std::tm tm_now {};
#if defined(_WIN32)
	localtime_s(&tm_now, &now);
#else
	localtime_r(&now, &tm_now);
#endif
	std::ostringstream output;
	output << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
	return output.str();
}

XER::flux::terminal::TerminalStyle StyleForLevel(LogLevel level) {
	using XER::flux::terminal::TerminalColor;
	using XER::flux::terminal::TerminalStyle;
	TerminalStyle style;
	switch (level) {
	case LogLevel::kDebug:
		style.foreground = TerminalColor::kBrightBlack;
		break;
	case LogLevel::kInfo:
		style.foreground = TerminalColor::kBrightCyan;
		break;
	case LogLevel::kWarning:
		style.foreground = TerminalColor::kBrightYellow;
		style.bold = true;
		break;
	case LogLevel::kError:
		style.foreground = TerminalColor::kBrightRed;
		style.bold = true;
		break;
	}
	return style;
}

}  // namespace

Logger::Logger(FluxConfig config)
	: config_(std::move(config)) {}

void Logger::UpdateConfig(const FluxConfig& config) {
	config_ = config;
}

const FluxConfig& Logger::config() const {
	return config_;
}

bool Logger::ShouldLog(LogLevel level) const {
	return static_cast<int>(level) >= static_cast<int>(config_.log_level);
}

void Logger::Log(LogLevel level, std::string_view component, std::string_view message) const {
	if (!ShouldLog(level)) {
		return;
	}
	std::ostringstream output;
	output << "[" << config_.application_name << "][" << ToString(level) << "]";
	if (config_.timestamps) {
		output << "[" << BuildTimestamp() << "]";
	}
	if (!component.empty()) {
		output << "[" << component << "]";
	}
	output << " " << message;
	std::string line = output.str();
	if (config_.colors) {
		line = XER::flux::terminal::ApplyStyle(line, StyleForLevel(level));
	}
	XER::flux::terminal::WriteLine(
		level == LogLevel::kWarning || level == LogLevel::kError
			? XER::flux::terminal::OutputStream::kStderr
			: XER::flux::terminal::OutputStream::kStdout,
		line);
	}

void Logger::Debug(std::string_view component, std::string_view message) const {
	Log(LogLevel::kDebug, component, message);
}

void Logger::Info(std::string_view component, std::string_view message) const {
	Log(LogLevel::kInfo, component, message);
}

void Logger::Warning(std::string_view component, std::string_view message) const {
	Log(LogLevel::kWarning, component, message);
}

void Logger::Error(std::string_view component, std::string_view message) const {
	Log(LogLevel::kError, component, message);
}

}  // namespace XER::flux::core
