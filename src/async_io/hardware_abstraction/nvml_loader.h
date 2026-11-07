#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace XER::AsyncIO::IO::Hardware {

// Snapshot of an NVIDIA GPU as reported by NVML (loaded dynamically, no link-time dependency).
struct NvmlDeviceSnapshot {
    int index = 0;
    std::string name;
    std::string uuid;
    std::string pci_bus_id;

    // Memory (bytes)
    uint64_t memory_total = 0;
    uint64_t memory_used  = 0;
    uint64_t memory_free  = 0;

    // Temperature (Celsius)
    double temp_celsius        = 0.0;
    bool   temp_supported      = false;
    double temp_slowdown_c     = 0.0;
    double temp_shutdown_c     = 0.0;
    bool   temp_thresholds_supported = false;

    // Power (watts)
    double power_watts       = 0.0;
    bool   power_supported   = false;
    double power_limit_watts = 0.0;
    bool   power_limit_supported = false;

    // Utilization (percent 0..100)
    double gpu_util_percent = 0.0;
    double mem_util_percent = 0.0;
    bool   util_supported   = false;

    // Clocks (MHz)
    uint32_t clock_graphics_mhz = 0;
    uint32_t clock_sm_mhz       = 0;
    uint32_t clock_mem_mhz      = 0;
    bool     clock_supported   = false;
};

// Process-wide NVML singleton. Loads libnvidia-ml (nvml.dll on Windows,
// libnvidia-ml.so.1 on Linux) at first use, initializes NVML once, and keeps
// it loaded for the lifetime of the process. All GPU queries in async_io
// should go through this singleton instead of loading NVML per call.
class NvmlLibrary {
public:
    static NvmlLibrary& Instance();

    // True if the library was found and nvmlInit succeeded.
    bool IsAvailable() const;

    // Number of NVIDIA GPUs reported by NVML (0 if unavailable).
    int GetDeviceCount() const;

    // Snapshot all devices. Empty if unavailable.
    std::vector<NvmlDeviceSnapshot> GetSnapshots() const;

    // Snapshot a single device. Returns false if unavailable / out of range.
    bool GetSnapshot(int device_index, NvmlDeviceSnapshot& out) const;

    NvmlLibrary(const NvmlLibrary&) = delete;
    NvmlLibrary& operator=(const NvmlLibrary&) = delete;

private:
    NvmlLibrary();
    ~NvmlLibrary();

    bool LoadAndInit();
    void Shutdown();

    struct Impl;
    Impl* impl_;
};

}  // namespace XER::AsyncIO::IO::Hardware
