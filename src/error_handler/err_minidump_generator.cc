#include "error_handler/err_minidump_generator.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unistd.h>

#include <absl/strings/str_cat.h>

#include "error_handler/err_stack_trace.h"

namespace XER::ErrorHandler {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

std::string CurrentTimestamp() {
	const auto tp = std::chrono::system_clock::now();
	const std::time_t t = std::chrono::system_clock::to_time_t(tp);
	std::ostringstream ss;
	ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
	return ss.str();
}

// Reads /proc/self/status and returns it as a string, or an empty string
// when unavailable (non-Linux platforms).
std::string ReadProcStatus() {
#if defined(__linux__)
	std::ifstream f("/proc/self/status");
	if (!f.is_open()) {
		return "(unavailable)";
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
#else
	return "(not supported on this platform)";
#endif
}

// Reads /proc/self/maps (memory layout) — truncated to first 4 KB.
std::string ReadProcMapsHead() {
#if defined(__linux__)
	std::ifstream f("/proc/self/maps");
	if (!f.is_open()) {
		return "(unavailable)";
	}
	constexpr std::size_t kMaxBytes = 4096;
	std::string data(kMaxBytes, '\0');
	f.read(data.data(), static_cast<std::streamsize>(kMaxBytes));
	data.resize(static_cast<std::size_t>(f.gcount()));
	return data;
#else
	return "(not supported on this platform)";
#endif
}

std::string BuildDumpText(const std::string& component,
			  const std::string& script_path,
			  const std::string& message) {
	std::ostringstream out;

	// ── Header ──────────────────────────────────────────────────────────────
	out << "=== XER PROCESS DUMP ===\n";
	out << "timestamp   : " << CurrentTimestamp() << "\n";
	out << "pid         : " << static_cast<long>(::getpid()) << "\n";
	if (!component.empty()) {
		out << "component   : " << component << "\n";
	}
	if (!script_path.empty()) {
		out << "script_path : " << script_path << "\n";
	}
	if (!message.empty()) {
		out << "message     : " << message << "\n";
	}
	out << "\n";

	// ── /proc/self/status ───────────────────────────────────────────────────
	out << "--- /proc/self/status ---\n";
	out << ReadProcStatus();
	out << "\n";

	// ── Stack trace ─────────────────────────────────────────────────────────
	out << "--- native stack trace ---\n";
	// skip_frames=2: skip Capture() itself + BuildDumpText().
	out << StackTrace::Capture(/*skip_frames=*/2, /*max_frames=*/64);
	out << "\n";

	// ── Memory map (head) ───────────────────────────────────────────────────
	out << "--- /proc/self/maps (first 4 KB) ---\n";
	out << ReadProcMapsHead();
	out << "\n=== END OF DUMP ===\n";

	return out.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// ErrMinidumpGenerator public API
// ---------------------------------------------------------------------------

void ErrMinidumpGenerator::SetComponent(absl::string_view component) {
	component_ = std::string(component);
}

void ErrMinidumpGenerator::SetScriptPath(absl::string_view script_path) {
	script_path_ = std::string(script_path);
}

void ErrMinidumpGenerator::SetMessage(absl::string_view message) {
	message_ = std::string(message);
}

bool ErrMinidumpGenerator::Generate(absl::string_view path) const {
	if (path.empty()) {
		return false;
	}

	const std::string dump_text = BuildDumpText(component_, script_path_, message_);

	std::ofstream out(std::string(path), std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out << dump_text;
	out.flush();
	return out.good();
}

}  // namespace XER::ErrorHandler
