#include "io_gpu_usage_logger.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif
  
namespace XER::AsyncIO::IO::LogDebug {

namespace {

#ifdef _WIN32
using DynamicLibraryHandle = HMODULE;

DynamicLibraryHandle OpenNvmlLibrary() {
    return LoadLibraryA("nvml.dll");
}

void* LoadNvmlSymbol(DynamicLibraryHandle library, const char* name) {
    return library != nullptr ? reinterpret_cast<void*>(GetProcAddress(library, name)) : nullptr;
}

void CloseNvmlLibrary(DynamicLibraryHandle library) {
    if (library != nullptr) {
        FreeLibrary(library);
    }
}
#elif defined(__linux__)
using DynamicLibraryHandle = void*;

DynamicLibraryHandle OpenNvmlLibrary() {
    return dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
}

void* LoadNvmlSymbol(DynamicLibraryHandle library, const char* name) {
    return library != nullptr ? dlsym(library, name) : nullptr;
}

void CloseNvmlLibrary(DynamicLibraryHandle library) {
    if (library != nullptr) {
        dlclose(library);
    }
}
#else
using DynamicLibraryHandle = void*;

DynamicLibraryHandle OpenNvmlLibrary() { return nullptr; }
void* LoadNvmlSymbol(DynamicLibraryHandle, const char*) { return nullptr; }
void CloseNvmlLibrary(DynamicLibraryHandle) {}
#endif

}  // namespace

GPUUsageLogger::GPUUsageLogger()
    : initialized_(false), is_logging_(false) {}

GPUUsageLogger::~GPUUsageLogger() {
    StopLogging();
}

bool GPUUsageLogger::Initialize() { 
    // Try to load NVML dynamically; if not present, mark as uninitialized.
    DynamicLibraryHandle lib = OpenNvmlLibrary();
    if (!lib) {
        initialized_ = false;
        return false;
    }
    using PfnInit = int (*)();
    auto pfnInit = reinterpret_cast<PfnInit>(LoadNvmlSymbol(lib, "nvmlInit_v2"));
    initialized_ = (pfnInit && pfnInit() == 0);
    if (initialized_) {
        using PfnShutdown = int (*)();
        if (auto pfnShutdown = reinterpret_cast<PfnShutdown>(LoadNvmlSymbol(lib, "nvmlShutdown")))
            pfnShutdown();
    }
    CloseNvmlLibrary(lib);
    return initialized_;
}

bool GPUUsageLogger::IsInitialized() const {
    return initialized_;
}

void GPUUsageLogger::StartLogging() {
    is_logging_ = true;
}

void GPUUsageLogger::StopLogging() {
    is_logging_ = false;
}

bool GPUUsageLogger::CaptureSnapshot(int device_id) {
    if (!initialized_) return false;

    GPUUsageSnapshot snapshot;
    if (QueryGPUUsage(device_id, snapshot)) {
        snapshots_.push_back(snapshot);
        return true;
    }
    
    return false;
}

std::vector<GPUUsageSnapshot> GPUUsageLogger::GetSnapshots(int device_id) const {
    std::vector<GPUUsageSnapshot> filtered;
    for (const auto& snap : snapshots_) {
        if (snap.device_id == device_id) {
            filtered.push_back(snap);
        }
    }
    return filtered;
}

double GPUUsageLogger::GetAverageUtilization(int device_id) const {
    auto snaps = GetSnapshots(device_id);
    if (snaps.empty()) return 0.0;

    double sum = 0.0;
    for (const auto& snap : snaps) {
        sum += snap.utilization_percent;
    }
    return sum / snaps.size();
}

double GPUUsageLogger::GetMaxUtilization(int device_id) const {
    auto snaps = GetSnapshots(device_id);
    double max_util = 0.0;
    
    for (const auto& snap : snaps) {
        if (snap.utilization_percent > max_util) {
            max_util = snap.utilization_percent;
        }
    }
    
    return max_util;
}

double GPUUsageLogger::GetAverageTemperature(int device_id) const {
    auto snaps = GetSnapshots(device_id);
    if (snaps.empty()) return 0.0;

    double sum = 0.0;
    for (const auto& snap : snaps) {
        sum += snap.temperature_celsius;
    }
    return sum / snaps.size();
}

double GPUUsageLogger::GetPeakMemoryUsage(int device_id) const {
    auto snaps = GetSnapshots(device_id);
    double peak = 0.0;
    
    for (const auto& snap : snaps) {
        if (snap.memory_used_mb > peak) {
            peak = snap.memory_used_mb;
        }
    }
    
    return peak;
}

std::string GPUUsageLogger::GetUsageReport(int device_id) const {
    std::string report;
    report += "=== GPU Usage Report (Device " + std::to_string(device_id) + ") ===\n";
    report += "Average Utilization: " + std::to_string(GetAverageUtilization(device_id)) + "%\n";
    report += "Peak Utilization: " + std::to_string(GetMaxUtilization(device_id)) + "%\n";
    report += "Average Temperature: " + std::to_string(GetAverageTemperature(device_id)) + "C\n";
    report += "Peak Memory Usage: " + std::to_string(GetPeakMemoryUsage(device_id)) + "MB\n";
    
    return report;
}

void GPUUsageLogger::ClearSnapshots() {
    snapshots_.clear();
}

bool GPUUsageLogger::QueryGPUUsage(int device_id, GPUUsageSnapshot& snapshot) {
    snapshot.device_id = device_id;
    snapshot.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Default zero values in case NVML is unavailable.
    snapshot.utilization_percent = 0.0;
    snapshot.memory_used_mb = 0.0;
    snapshot.memory_free_mb = 0.0;
    snapshot.temperature_celsius = 0.0;
    snapshot.power_draw_watts = 0.0;

    DynamicLibraryHandle lib = OpenNvmlLibrary();
    if (!lib) return true;  // return true with zeros (non-NVIDIA machine)

    using nvmlReturn_t = int;
    using nvmlDevice_t = void*;
    constexpr nvmlReturn_t kOk = 0;

    struct NvmlUtilization { unsigned int gpu, memory; };
    struct NvmlMemInfo     { unsigned long long total, free, used; };

    using PfnInit     = nvmlReturn_t (*)();
    using PfnHandle   = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using PfnUtil     = nvmlReturn_t (*)(nvmlDevice_t, NvmlUtilization*);
    using PfnMem      = nvmlReturn_t (*)(nvmlDevice_t, NvmlMemInfo*);
    using PfnTemp     = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);
    using PfnPower    = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
    using PfnShutdown = nvmlReturn_t (*)();

    auto pfnInit     = reinterpret_cast<PfnInit>    (LoadNvmlSymbol(lib, "nvmlInit_v2"));
    auto pfnHandle   = reinterpret_cast<PfnHandle>  (LoadNvmlSymbol(lib, "nvmlDeviceGetHandleByIndex_v2"));
    auto pfnUtil     = reinterpret_cast<PfnUtil>    (LoadNvmlSymbol(lib, "nvmlDeviceGetUtilizationRates"));
    auto pfnMem      = reinterpret_cast<PfnMem>     (LoadNvmlSymbol(lib, "nvmlDeviceGetMemoryInfo"));
    auto pfnTemp     = reinterpret_cast<PfnTemp>    (LoadNvmlSymbol(lib, "nvmlDeviceGetTemperature"));
    auto pfnPower    = reinterpret_cast<PfnPower>   (LoadNvmlSymbol(lib, "nvmlDeviceGetPowerUsage"));
    auto pfnShutdown = reinterpret_cast<PfnShutdown>(LoadNvmlSymbol(lib, "nvmlShutdown"));

    bool ok = false;
    if (pfnInit && pfnInit() == kOk) {
        nvmlDevice_t dev = nullptr;
        if (pfnHandle && pfnHandle(static_cast<unsigned int>(device_id), &dev) == kOk && dev) {
            ok = true;
            if (pfnUtil) {
                NvmlUtilization u{};
                if (pfnUtil(dev, &u) == kOk)
                    snapshot.utilization_percent = static_cast<double>(u.gpu);
            }
            if (pfnMem) {
                NvmlMemInfo m{};
                if (pfnMem(dev, &m) == kOk) {
                    snapshot.memory_used_mb = static_cast<double>(m.used) / (1024.0 * 1024.0);
                    snapshot.memory_free_mb = static_cast<double>(m.free) / (1024.0 * 1024.0);
                }
            }
            if (pfnTemp) {
                unsigned int temp = 0;
                if (pfnTemp(dev, 0 /* NVML_TEMPERATURE_GPU */, &temp) == kOk)
                    snapshot.temperature_celsius = static_cast<double>(temp);
            }
            if (pfnPower) {
                unsigned int mw = 0;
                if (pfnPower(dev, &mw) == kOk)
                    snapshot.power_draw_watts = static_cast<double>(mw) / 1000.0;
            }
        }
        if (pfnShutdown) pfnShutdown();
    }
    CloseNvmlLibrary(lib);
    return ok || true;  // always succeed; caller can use zeros for missing data
}

}  // namespace XER::AsyncIO::IO::LogDebug
