#pragma once

#include "openai_types.h"
#include "../../../async_io/hardware_abstraction/gpu_info.h"
#include "../../../async_io/hardware_abstraction/cpu_info.h"
#include "../../../async_io/hardware_abstraction/memory_monitor.h"
#include <memory>
#include <string>
#include <functional>

namespace Content::OpenAI::Hardware {

/**
 * Hardware resources for OpenAI operations
 */
struct HardwareResources {
    int available_cpu_cores;
    int gpu_device_count;
    uint64_t total_memory_mb;
    uint64_t free_memory_mb;
    bool has_cuda;
    bool has_opencl;
    bool has_hip;
    bool has_vulkan;
    std::string gpu_backend;
};

/**
 * Hardware manager for OpenAI operations
 *
 * Integrates with XER hardware abstraction to:
 * - Detect available GPU resources
 * - Allocate memory for operations
 * - Monitor resource usage
 * - Optimize based on hardware capabilities
 */
class HardwareManager {
public:
    HardwareManager();
    ~HardwareManager() = default;

    bool Initialize();
    bool IsInitialized() const;

    // Resource information
    HardwareResources GetResources() const;
    std::string GetHardwareReport() const;

    // GPU management
    bool SelectGPU(int device_id);
    int GetSelectedGPU() const;
    uint64_t GetGPUMemory(int device_id) const;

    // Memory management
    bool AllocateMemory(size_t size_mb);
    void FreeMemory();
    size_t GetAllocatedMemory() const;

    // Resource monitoring
    void StartMonitoring();
    void StopMonitoring();
    double GetCPUUsage() const;
    double GetMemoryUsage() const;
    double GetGPUUsage() const;

    // Optimization
    void EnableGPUAcceleration(bool enable);
    bool IsGPUAccelerationEnabled() const;
    void SetMaxMemoryUsage(double percentage);

    // Callbacks
    using ResourceWarningCallback = std::function<void(const std::string& warning)>;
    void OnResourceWarning(ResourceWarningCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * GPU-accelerated image processing
 */
class GPUImageProcessor {
public:
    GPUImageProcessor();
    ~GPUImageProcessor() = default;

    bool Initialize(std::shared_ptr<HardwareManager> hw_manager);
    bool IsInitialized() const;

    // GPU-accelerated operations
    bool ResizeImage(uint8_t* data, int width, int height, int new_width, int new_height);
    bool ApplyFilter(uint8_t* data, int width, int height, const std::string& filter_type);
    bool ConvertFormat(uint8_t* data, int width, int height, const std::string& target_format);

    // Performance
    double GetLastOperationTime() const;
    std::string GetPerformanceReport() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Memory pool for efficient buffer management
 */
class MemoryPool {
public:
    MemoryPool(size_t pool_size_mb);
    ~MemoryPool() = default;

    void* Allocate(size_t size);
    void Free(void* ptr);
    void Clear();

    size_t GetUsedMemory() const;
    size_t GetFreeMemory() const;
    size_t GetTotalMemory() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace Content::OpenAI::Hardware
