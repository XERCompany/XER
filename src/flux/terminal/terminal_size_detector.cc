#include "flux/terminal/terminal_size_detector.h"

#include <algorithm>
#include <cstdlib>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace XER::Flux::Terminal {
namespace {

std::size_t ParsePositiveEnv(const char* key, std::size_t fallback) {
	const char* raw = std::getenv(key);
	if (raw == nullptr || raw[0] == '\0') {
		return fallback;
	}
	char* end = nullptr;
	const unsigned long parsed = std::strtoul(raw, &end, 10);
	if (end == raw || parsed == 0UL) {
		return fallback;
	}
	return static_cast<std::size_t>(parsed);
}

}  // namespace

TerminalSize DefaultTerminalSize() {
	return TerminalSize{};
}

TerminalSize DetectTerminalSize() {
	TerminalSize size = DefaultTerminalSize();

#if defined(__unix__) || defined(__APPLE__)
	if (::isatty(STDOUT_FILENO) == 1) {
		size.interactive = true;
		struct winsize window_size {};
		if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size) == 0) {
			if (window_size.ws_col > 0) {
				size.columns = static_cast<std::size_t>(window_size.ws_col);
			}
			if (window_size.ws_row > 0) {
				size.rows = static_cast<std::size_t>(window_size.ws_row);
			}
		}
	}
#endif

	size.columns = ParsePositiveEnv("COLUMNS", size.columns);
	size.rows = ParsePositiveEnv("LINES", size.rows);
	return ClampTerminalSize(size, 20, 5);
}

TerminalSize ClampTerminalSize(const TerminalSize& size, std::size_t min_columns, std::size_t min_rows) {
	TerminalSize clamped = size;
	clamped.columns = std::max(min_columns, clamped.columns);
	clamped.rows = std::max(min_rows, clamped.rows);
	return clamped;
}

}  // namespace XER::Flux::Terminal
