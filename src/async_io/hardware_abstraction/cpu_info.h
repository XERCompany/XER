#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace AsyncIO::IO::Hardware {

struct ProcessorCore {
    int core_id;            // Physical core index (0-based); same for sibling SMT threads
    int logical_id;         // Logical processor (SMT thread) index
    uint64_t frequency_mhz; // Nominal/base frequency (0 if unknown)
    uint64_t current_frequency_mhz; // Current frequency (0 if unsupported)
    bool is_active;
};

struct CPUInfo {
    std::string vendor;
    std::string model;
    int physical_cores;
    int logical_cores;
    uint64_t l1_cache_kb;
    uint64_t l2_cache_kb;
    uint64_t l3_cache_kb;
    bool supports_sse;
    bool supports_avx;
    bool supports_avx2;
    bool supports_avx512;
};

class CPUInfoProvider {
public:
    CPUInfoProvider();
    ~CPUInfoProvider();

    // CPU detection
    bool Initialize();
    bool IsInitialized() const;

    // Information
    const CPUInfo& GetInfo() const { return cpu_info_; }
    std::vector<ProcessorCore> GetCores() const;
    uint64_t GetTotalMemoryMB() const;
    uint64_t GetAvailableMemoryMB() const;

    // Core affinity
    bool SetThreadAffinity(int core_id);
    int GetCurrentCore() const;

    // Performance monitoring
    double GetCPUUsagePercent() const;
    double GetCoreUsagePercent(int core_id) const;

    // Refresh per-core current frequencies (Linux sysfs / Windows best-effort).
    // Base/nominal frequencies are populated at Initialize(); current
    // frequencies require a fresh query and are updated by this method.
    void RefreshFrequencies();

    // Info string
    std::string GetCPUDescription() const;

private:
    CPUInfo cpu_info_;
    std::vector<ProcessorCore> cores_;
    bool initialized_;
    
    void DetectCPUCapabilities();
    void DetectCoreCount();
};

}  // namespace AsyncIO::IO::Hardware
