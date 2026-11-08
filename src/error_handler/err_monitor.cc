#include "error_handler/err_monitor.h"

#include <cstdlib>
#include <ctime>
#include <exception>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/any_of.hpp>

#include "cache/cache_constants.h"
#include "cache/cache_manager.h"
#include "error_handler/err_capture.h"
#include "error_handler/err_report_builder.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "helper/string.h"

namespace XER::ErrorHandler {
namespace {

std::mutex& MonitorMutex() {
	static std::mutex mutex;
	return mutex;
}

MonitorConfiguration& MutableConfiguration() {
	static MonitorConfiguration configuration;
	return configuration;
}

bool& MonitorInstalled() {
	static bool installed = false;
	return installed;
}

std::string& MutableLastReportPath() {
	static std::string path;
	return path;
}

std::string TimestampPrefix() {
	if (!MutableConfiguration().timestamps) {
		return std::string();
	}

	const std::time_t now = std::time(nullptr);
	std::tm local_time{};
	localtime_r(&now, &local_time);
	std::ostringstream out;
	out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << " ";
	return out.str();
}

std::string FormatLine(std::string_view level,
					 std::string_view component,
					 std::string_view message) {
	const absl::string_view level_view(level.data(), level.size());
	const absl::string_view component_view(component.data(), component.size());
	const absl::string_view message_view(message.data(), message.size());
	return absl::StrCat(
		TimestampPrefix(),
		"[", level_view, "] ",
		Helper::String::DefaultString(component_view, "startup"),
		": ",
		Helper::String::NormalizeUtf8(message_view));
}

DiagnosticData PopulateRuntimeContext(DiagnosticData diagnostic) {
	const CaptureConfiguration capture = CurrentCaptureConfiguration();
	diagnostic.script_path = capture.script_path;
	diagnostic.dump_dir = capture.dump_dir;
	diagnostic.crash_dump_path = LastCrashDumpPath();
	return diagnostic;
}

std::string PersistDiagnosticReport(const DiagnosticData& diagnostic) {
	if (!MutableConfiguration().persist_reports) {
		return std::string();
	}

	std::string error;
	const std::string key = BuildDiagnosticCacheKey(diagnostic);
	const std::string file_name = BuildDiagnosticFileName(diagnostic);
	const std::string report = BuildDiagnosticReport(diagnostic);
	auto& cache = XER::Cache::CacheManager::Instance();
	if (!cache.WritePersistentText(XER::Cache::constants::kErrorReportScope, key, file_name, report, &error)) {
		flux::terminal::WriteLine(flux::terminal::OutputStream::kStderr,
			FormatLine("error", "error_handler", absl::StrCat("failed to persist diagnostic report: ", error)));
		return std::string();
	}
	return cache.PathFor(XER::Cache::constants::kErrorReportScope, key, file_name).string();
}

std::string FormatDiagnosticLine(const DiagnosticData& diagnostic) {
	std::string line = FormatLine(diagnostic.level, diagnostic.component, diagnostic.message);
	if (!diagnostic.crash_dump_path.empty()) {
		line = absl::StrCat(line, " [dump=", diagnostic.crash_dump_path, "]");
	}
	return line;
}

bool ShouldEmitInfo() {
	const MonitorConfiguration& config = MutableConfiguration();
	if (config.verbose) {
		return true;
	}
	const std::string level = Helper::String::CanonicalizeToken(config.log_level);
	return ranges::any_of(std::initializer_list<std::string_view>{"info", "debug", "trace"},
		[&level](std::string_view candidate) {
			return level == candidate;
		});
}

[[noreturn]] void HandleTerminate() {
	std::string detail = "std::terminate invoked";
	if (std::exception_ptr current = std::current_exception(); current != nullptr) {
		try {
			std::rethrow_exception(current);
		} catch (const std::exception& error) {
			detail = absl::StrCat(detail, ": ", error.what());
		} catch (...) {
			detail = absl::StrCat(detail, ": unknown exception");
		}
	}
	ReportDiagnostic(PopulateRuntimeContext(MakeDiagnosticData("fatal", "error_handler", detail)));
	std::abort();
}

}  // namespace

void InitializeMonitor(const MonitorConfiguration& config) {
	bool should_emit_initialized = false;
	{
		std::lock_guard<std::mutex> lock(MonitorMutex());
		MutableConfiguration() = config;
		if (!MonitorInstalled()) {
			std::set_terminate(&HandleTerminate);
			MonitorInstalled() = true;
		}
		should_emit_initialized = ShouldEmitInfo();
	}
	if (should_emit_initialized) {
		ReportStartupEvent("error_handler", "monitor initialized");
	}
}

void ReportDiagnostic(const DiagnosticData& diagnostic) {
	const DiagnosticData complete = PopulateRuntimeContext(diagnostic);
	flux::terminal::WriteLine(flux::terminal::OutputStream::kStderr, FormatDiagnosticLine(complete));
	MutableLastReportPath() = PersistDiagnosticReport(complete);
}

void ReportStartupEvent(std::string_view component, std::string_view message) {
	if (!ShouldEmitInfo()) {
		return;
	}
	ReportDiagnostic(MakeDiagnosticData("info", component, message));
}

void ReportStartupError(std::string_view component, std::string_view message) {
	ReportDiagnostic(MakeDiagnosticData("error", component, message));
}

void ReportException(std::string_view component, std::string_view message) {
	ReportDiagnostic(MakeDiagnosticData("fatal", component, message));
}

std::string LastReportPath() {
	std::lock_guard<std::mutex> lock(MonitorMutex());
	return MutableLastReportPath();
}

}  // namespace XER::ErrorHandler
