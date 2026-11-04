#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace AsyncIO::IO::Hardware {

struct GPUDevice {
    int device_id;
    std::string name;
    std::string compute_capability;
    uint64_t total_memory_mb;
    uint64_t free_memory_mb;
    bool is_available;
    uint32_t vendor_id = 0;
    uint32_t pci_device_id = 0;
    std::string driver_version;
    std::string backend;

    // Live telemetry from NVML (NVIDIA only; zero/unsupported otherwise).
    double temp_celsius = 0.0;
    bool   temp_supported = false;
    double power_watts = 0.0;
    bool   power_supported = false;
    double gpu_util_percent = 0.0;
    double mem_util_percent = 0.0;
    bool   util_supported = false;
    uint32_t clock_graphics_mhz = 0;
    uint32_t clock_mem_mhz = 0;
};

class GPUInfoProvider {
public:
    GPUInfoProvider();
    ~GPUInfoProvider();

    // GPU detection
    bool Initialize();
    bool IsInitialized() const;

    // Information
    int GetDeviceCount() const;
    std::vector<GPUDevice> GetDevices() const;
    std::string GetDeviceName(int device_id) const;
    uint64_t GetTotalMemory(int device_id) const;
    uint64_t GetFreeMemory(int device_id) const;

    // Device selection
    bool SetCurrentDevice(int device_id);
    int GetCurrentDevice() const;

    // Capabilities
    bool SupportsCUDA() const { return supports_cuda_; }
    bool SupportsOpenCL() const { return supports_opencl_; }
    bool SupportsHIP() const { return supports_hip_; }
    bool SupportsVulkan() const { return supports_vulkan_; }

    // Device info
    std::string GetDeviceDescription(int device_id) const;

private:
    std::vector<GPUDevice> gpu_devices_;
    int current_device_;
    bool initialized_;
    bool supports_cuda_;
    bool supports_opencl_;
    bool supports_hip_;
    bool supports_vulkan_;

    void DetectGPUs();
    void DetectVulkanDevices();
    void DetectCUDADevices();
    void DetectNvmlDevices();
};

}  // namespace AsyncIO::IO::Hardware
