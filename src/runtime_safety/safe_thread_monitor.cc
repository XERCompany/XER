#include "runtime_safety/safe_thread_monitor.h"

#include <string>
#include <vector>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_split.h>
#include <absl/strings/numbers.h>

#if defined(__linux__)
#  include <pthread.h>
#  include <sched.h>
#endif

namespace XER::RuntimeSafety {

ThreadMonitorSnapshot CaptureThreadMonitorSnapshot(const EngineParams& params,
						   std::size_t resolved_async_workers,
						   std::size_t resolved_queue_depth) {
	ThreadMonitorSnapshot snapshot;
	snapshot.async_io_workers = resolved_async_workers;
	snapshot.async_io_queue_depth = resolved_queue_depth;
	snapshot.max_cpu_threads = params.max_cpu_threads;
	snapshot.v8_platform_workers = params.v8_platform_workers;
	snapshot.cpu_affinity = params.cpu_affinity;
	snapshot.thread_priority = params.thread_priority;
	return snapshot;
}

std::string BuildThreadMonitorSummary(const ThreadMonitorSnapshot& snapshot) {
	return absl::StrCat(
		"thread_monitor(async_workers=", snapshot.async_io_workers,
		", queue_depth=", snapshot.async_io_queue_depth,
		", max_cpu_threads=", snapshot.max_cpu_threads,
		", v8_workers=", snapshot.v8_platform_workers,
		", cpu_affinity=", snapshot.cpu_affinity.empty() ? "auto" : snapshot.cpu_affinity,
		", priority=", snapshot.thread_priority,
		")");
}

// ---------------------------------------------------------------------------
// CPU affinity helpers
// ---------------------------------------------------------------------------

std::vector<int> ParseCpuAffinityString(const std::string& affinity) {
	if (affinity.empty()) {
		return {};
	}
	std::vector<int> result;
	// Split by comma first, then handle ranges like "0-3".
	for (absl::string_view segment :
	     absl::StrSplit(affinity, ',', absl::SkipWhitespace())) {
		const auto dash = segment.find('-');
		if (dash != absl::string_view::npos) {
			// Range: "lo-hi"
			int lo = 0, hi = 0;
			if (!absl::SimpleAtoi(segment.substr(0, dash), &lo) ||
			    !absl::SimpleAtoi(segment.substr(dash + 1), &hi) ||
			    lo < 0 || hi < lo) {
				return {};  // parse error
			}
			for (int cpu = lo; cpu <= hi; ++cpu) {
				result.push_back(cpu);
			}
		} else {
			int cpu = 0;
			if (!absl::SimpleAtoi(segment, &cpu) || cpu < 0) {
				return {};  // parse error
			}
			result.push_back(cpu);
		}
	}
	return result;
}

bool ApplyCpuAffinity(const std::vector<int>& cpus, std::string* error_out) {
	if (cpus.empty()) {
		return true;
	}
#if defined(__linux__)
	cpu_set_t set;
	CPU_ZERO(&set);
	for (int cpu : cpus) {
		if (cpu < 0 || cpu >= CPU_SETSIZE) {
			if (error_out != nullptr) {
				*error_out = absl::StrCat("CPU index out of range: ", cpu);
			}
			return false;
		}
		CPU_SET(cpu, &set);
	}
	const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
	if (rc != 0) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat(
				"pthread_setaffinity_np failed: errno=", rc);
		}
		return false;
	}
	return true;
#else
	(void)error_out;
	return true;  // Not supported; silently succeed.
#endif
}

bool ApplyCpuAffinityString(const std::string& affinity,
			    std::string* error_out) {
	if (affinity.empty()) {
		return true;
	}
	const std::vector<int> cpus = ParseCpuAffinityString(affinity);
	if (cpus.empty()) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat(
				"Failed to parse cpu_affinity string: '", affinity, "'");
		}
		return false;
	}
	return ApplyCpuAffinity(cpus, error_out);
}

}  // namespace XER::RuntimeSafety
