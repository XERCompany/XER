#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace XER::ML::Utils {

/// @brief Description of one registered ggml backend device.
struct GgmlBackendInfo {
    std::string name;
    std::string description;
    bool is_gpu = false;
};

/// @brief Snapshot of the ggml backend registry (CPU/CUDA/Vulkan/...).
struct GgmlRuntimeInfo {
    bool available = false;
    size_t backend_count = 0;
    size_t cpu_count = 0;
    size_t gpu_count = 0;
    size_t accel_count = 0;
    std::vector<GgmlBackendInfo> backends;
    size_t recommended_threads = 1;
};

/// @brief Probe the ggml backend registry. Safe to call when ggml is absent —
/// returns {available=false}. Loads dynamic backends once per process.
GgmlRuntimeInfo ProbeGgmlRuntime();

/// @brief One-line human-readable summary for logs/feature banners.
std::string GgmlRuntimeSummary();

} // namespace XER::ML::Utils
