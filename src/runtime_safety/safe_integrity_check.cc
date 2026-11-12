#include "runtime_safety/safe_integrity_check.h"

#include <algorithm>
#include <string>
#include <vector>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>

#include "async_io/io_thread_pool.h"
#include "cache/cache_configuration.h"
#include "engine_params.h"
#include "error_handler/err_monitor.h"
#include "helper/string_utils.h"
#include "runtime_safety/safe_memory_guard.h"
#include "runtime_safety/safe_sandbox.h"
#include "runtime_safety/safe_thread_monitor.h"

namespace XER::RuntimeSafety {
namespace {

std::size_t ResolveQueueDepth(const StartupOptions& options, std::size_t worker_count) {
	if (options.async_io_queue_depth > 0) {
		return static_cast<std::size_t>(options.async_io_queue_depth);
	}
	return std::max<std::size_t>(8, worker_count * 8);
}

std::string BuildStartupSummary(const StartupOptions& options, const StartupState& state) {
	std::vector<std::string> lines = {
		Helper::String::BuildKeyValueLine("cache_dir", state.cache_directory.string()),
		Helper::String::BuildKeyValueLine("async_io_workers", std::to_string(state.async_io_workers)),
		Helper::String::BuildKeyValueLine("async_io_queue_depth", std::to_string(state.async_io_queue_depth)),
		Helper::String::BuildKeyValueLine("sandbox", state.sandbox_enabled ? "enabled" : "disabled"),
		options.script_path.empty() ? std::string() : Helper::String::BuildKeyValueLine("script", options.script_path.string())
	};
	std::vector<std::string> filtered;
	filtered.reserve(lines.size());
	for (const std::string& line : lines) {
		if (!Helper::String::IsBlank(line)) {
			filtered.push_back(line);
		}
	}
	return absl::StrJoin(filtered, ", ");
}

}  // namespace

bool BootstrapStartup(const StartupOptions& options, StartupState* state, std::string* error_out) {
	if (state == nullptr) {
		if (error_out != nullptr) {
			*error_out = "Startup state target is null";
		}
		return false;
	}

	const XER::Cache::CacheConfiguration cache_config = XER::Cache::CacheConfigurationFromEnvironment();
	const EngineParams params = EngineParamsFromEnv();
	const MemoryGuardSnapshot memory_guard = CaptureMemoryGuardSnapshot(params);
	if (!ValidateMemoryGuardSnapshot(memory_guard, error_out)) {
		XER::ErrorHandler::ReportStartupError("runtime_safety",
			error_out != nullptr ? *error_out : std::string("Invalid memory guard configuration"));
		return false;
	}
	const SandboxSettings sandbox_settings = ResolveSandboxSettings(params);
	if (!XER::Cache::PrepareCacheDirectory(cache_config, error_out)) {
		XER::ErrorHandler::ReportStartupError("runtime_safety",
			error_out != nullptr ? *error_out : std::string("Failed to prepare cache directory"));
		return false;
	}

	XER::AsyncIO::IO::Async::IOThreadPool& thread_pool =
		XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance();

	StartupState resolved_state;
	resolved_state.cache_directory = cache_config.directory;
	resolved_state.async_io_workers = thread_pool.GetThreadCount();
	resolved_state.async_io_queue_depth = ResolveQueueDepth(options, resolved_state.async_io_workers);
	resolved_state.sandbox_enabled = sandbox_settings.enabled;
	*state = resolved_state;

	if (options.verbose) {
		const ThreadMonitorSnapshot thread_monitor = CaptureThreadMonitorSnapshot(
			params,
			resolved_state.async_io_workers,
			resolved_state.async_io_queue_depth);
		std::vector<std::string> details = {
			BuildStartupSummary(options, resolved_state),
			BuildMemoryGuardSummary(memory_guard),
			BuildSandboxSummary(sandbox_settings),
			BuildThreadMonitorSummary(thread_monitor)
		};
		XER::ErrorHandler::ReportStartupEvent("runtime_safety", absl::StrJoin(details, ", "));
	}
	return true;
}

}  // namespace XER::RuntimeSafety
