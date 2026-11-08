#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "engine_params.h"

namespace XER::RuntimeSafety {

struct ThreadMonitorSnapshot {
	std::size_t async_io_workers = 0;
	std::size_t async_io_queue_depth = 0;
	int max_cpu_threads = -1;
	int v8_platform_workers = -1;
	std::string cpu_affinity;
	int thread_priority = 0;
};

ThreadMonitorSnapshot CaptureThreadMonitorSnapshot(const EngineParams& params,
						   std::size_t resolved_async_workers,
						   std::size_t resolved_queue_depth);
std::string BuildThreadMonitorSummary(const ThreadMonitorSnapshot& snapshot);

// Parse a CPU affinity string such as "0", "0,2,4" or "0-3" into a sorted
// list of zero-based CPU indices.  Returns an empty vector on parse failure.
std::vector<int> ParseCpuAffinityString(const std::string& affinity);

// Apply CPU affinity to the calling thread using the parsed CPU list.
// Returns false with |error_out| set if the call fails.
bool ApplyCpuAffinity(const std::vector<int>& cpus, std::string* error_out);

// Convenience wrapper: parse |affinity| and apply it to the calling thread.
// No-op (returns true) when |affinity| is empty.
bool ApplyCpuAffinityString(const std::string& affinity, std::string* error_out);

}  // namespace XER::RuntimeSafety
