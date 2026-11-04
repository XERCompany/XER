#include "nvml_loader.h"

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include <cstring>

namespace AsyncIO::IO::Hardware {

// Minimal NVML ABI typedefs (kept opaque so we never include nvml.h).
namespace {

using NvmlReturn_t   = int;
using NvmlDevice_t    = void*;
constexpr NvmlReturn_t kNvmlSuccess = 0;

// nvmlTemperatureSensors_t
constexpr int kNvmlTempGpu = 0;
// nvmlTemperatureThresholds_t
constexpr int kNvmlTempThresholdSlowdown = 1;
constexpr int kNvmlTempThresholdShutdown = 2;
// nvmlClockType_t
constexpr int kNvmlClockGraphics = 0;
constexpr int kNvmlClockSm       = 1;
constexpr int kNvmlClockMem      = 2;

struct NvmlMemory {
    unsigned long long total;
    unsigned long long reserved;
    unsigned long long free;
    unsigned long long used;
};

struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};

using PfnInit        = NvmlReturn_t (*)();
using PfnShutdown    = NvmlReturn_t (*)();
using PfnCount       = NvmlReturn_t (*)(unsigned int*);
using PfnHandle      = NvmlReturn_t (*)(unsigned int, NvmlDevice_t*);
using PfnName        = NvmlReturn_t (*)(NvmlDevice_t, char*, unsigned int);
using PfnUuid        = NvmlReturn_t (*)(NvmlDevice_t, char*, unsigned int);
using PfnPciBus      = NvmlReturn_t (*)(NvmlDevice_t, char*, unsigned int);
using PfnMemInfo     = NvmlReturn_t (*)(NvmlDevice_t, NvmlMemory*);
using PfnTemp        = NvmlReturn_t (*)(NvmlDevice_t, int, unsigned int*);
using PfnTempThresh  = NvmlReturn_t (*)(NvmlDevice_t, int, unsigned int*);
using PfnPowerUsage  = NvmlReturn_t (*)(NvmlDevice_t, unsigned int*);
using PfnPowerLimit  = NvmlReturn_t (*)(NvmlDevice_t, unsigned int*);
using PfnUtilization = NvmlReturn_t (*)(NvmlDevice_t, NvmlUtilization*);
using PfnClock       = NvmlReturn_t (*)(NvmlDevice_t, int, unsigned int*);

}  // namespace

struct NvmlLibrary::Impl {
    void* lib = nullptr;
    bool initialized = false;

    PfnInit         pfnInit        = nullptr;
    PfnShutdown     pfnShutdown    = nullptr;
    PfnCount        pfnCount       = nullptr;
    PfnHandle       pfnHandle      = nullptr;
    PfnName         pfnName        = nullptr;
    PfnUuid         pfnUuid        = nullptr;
    PfnPciBus       pfnPciBus      = nullptr;
    PfnMemInfo      pfnMemInfo     = nullptr;
    PfnTemp         pfnTemp        = nullptr;
    PfnTempThresh   pfnTempThresh  = nullptr;
    PfnPowerUsage   pfnPowerUsage  = nullptr;
    PfnPowerLimit   pfnPowerLimit  = nullptr;
    PfnUtilization  pfnUtilization = nullptr;
    PfnClock        pfnClock       = nullptr;

    void* Sym(const char* name) {
        if (!lib) return nullptr;
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
        return dlsym(lib, name);
#endif
    }
};

NvmlLibrary& NvmlLibrary::Instance() {
    static NvmlLibrary instance;
    return instance;
}

NvmlLibrary::NvmlLibrary() : impl_(new Impl()) {
    LoadAndInit();
}

NvmlLibrary::~NvmlLibrary() {
    Shutdown();
    if (impl_->lib) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(impl_->lib));
#else
        dlclose(impl_->lib);
#endif
        impl_->lib = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

bool NvmlLibrary::LoadAndInit() {
    if (impl_->initialized) return true;
    if (impl_->lib) return true;

#if defined(_WIN32)
    impl_->lib = static_cast<void*>(LoadLibraryA("nvml.dll"));
#else
    impl_->lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!impl_->lib) impl_->lib = dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL);
#endif
    if (!impl_->lib) return false;

    // Resolve symbols. Init/Shutdown + count/handle are required; the rest
    // are optional and callers must check the relevant *_supported flags.
    impl_->pfnInit        = reinterpret_cast<PfnInit>(impl_->Sym("nvmlInit_v2"));
    if (!impl_->pfnInit) impl_->pfnInit = reinterpret_cast<PfnInit>(impl_->Sym("nvmlInit"));
    impl_->pfnShutdown    = reinterpret_cast<PfnShutdown>(impl_->Sym("nvmlShutdown"));
    impl_->pfnCount       = reinterpret_cast<PfnCount>(impl_->Sym("nvmlDeviceGetCount_v2"));
    if (!impl_->pfnCount) impl_->pfnCount = reinterpret_cast<PfnCount>(impl_->Sym("nvmlDeviceGetCount"));
    impl_->pfnHandle      = reinterpret_cast<PfnHandle>(impl_->Sym("nvmlDeviceGetHandleByIndex_v2"));
    if (!impl_->pfnHandle) impl_->pfnHandle = reinterpret_cast<PfnHandle>(impl_->Sym("nvmlDeviceGetHandleByIndex"));

    if (!impl_->pfnInit || !impl_->pfnCount || !impl_->pfnHandle) {
        // Not a usable NVML — give up on this library.
        return false;
    }

    impl_->pfnName        = reinterpret_cast<PfnName>(impl_->Sym("nvmlDeviceGetName"));
    impl_->pfnUuid        = reinterpret_cast<PfnUuid>(impl_->Sym("nvmlDeviceGetUUID"));
    impl_->pfnPciBus      = reinterpret_cast<PfnPciBus>(impl_->Sym("nvmlDeviceGetPciInfoV1"));
    if (!impl_->pfnPciBus) impl_->pfnPciBus = reinterpret_cast<PfnPciBus>(impl_->Sym("nvmlDeviceGetPciInfo"));
    impl_->pfnMemInfo     = reinterpret_cast<PfnMemInfo>(impl_->Sym("nvmlDeviceGetMemoryInfo"));
    impl_->pfnTemp        = reinterpret_cast<PfnTemp>(impl_->Sym("nvmlDeviceGetTemperature"));
    impl_->pfnTempThresh  = reinterpret_cast<PfnTempThresh>(impl_->Sym("nvmlDeviceGetTemperatureThreshold"));
    impl_->pfnPowerUsage  = reinterpret_cast<PfnPowerUsage>(impl_->Sym("nvmlDeviceGetPowerUsage"));
    impl_->pfnPowerLimit  = reinterpret_cast<PfnPowerLimit>(impl_->Sym("nvmlDeviceGetPowerManagementLimit"));
    impl_->pfnUtilization = reinterpret_cast<PfnUtilization>(impl_->Sym("nvmlDeviceGetUtilizationRates"));
    impl_->pfnClock       = reinterpret_cast<PfnClock>(impl_->Sym("nvmlDeviceGetClockInfo"));

    if (impl_->pfnInit() != kNvmlSuccess) {
        return false;
    }
    impl_->initialized = true;
    return true;
}

void NvmlLibrary::Shutdown() {
    if (impl_->initialized && impl_->pfnShutdown) {
        impl_->pfnShutdown();
        impl_->initialized = false;
    }
}

bool NvmlLibrary::IsAvailable() const {
    return impl_->initialized;
}

int NvmlLibrary::GetDeviceCount() const {
    if (!impl_->initialized) return 0;
    unsigned int count = 0;
    if (impl_->pfnCount(&count) != kNvmlSuccess) return 0;
    return static_cast<int>(count);
}

bool NvmlLibrary::GetSnapshot(int device_index, NvmlDeviceSnapshot& out) const {
    if (!impl_->initialized) return false;
    if (device_index < 0) return false;

    NvmlDevice_t handle = nullptr;
    if (impl_->pfnHandle(static_cast<unsigned int>(device_index), &handle) != kNvmlSuccess || !handle) {
        return false;
    }

    out = NvmlDeviceSnapshot{};
    out.index = device_index;

    char buf[256] = {};
    if (impl_->pfnName) {
        if (impl_->pfnName(handle, buf, sizeof(buf)) == kNvmlSuccess) {
            out.name = buf;
        }
    }
    if (impl_->pfnUuid) {
        if (impl_->pfnUuid(handle, buf, sizeof(buf)) == kNvmlSuccess) {
            out.uuid = buf;
        }
    }
    if (impl_->pfnPciBus) {
        if (impl_->pfnPciBus(handle, buf, sizeof(buf)) == kNvmlSuccess) {
            // nvmlDeviceGetPciInfoV1 fills a struct; the simple char* variant
            // isn't ABI-stable, so we keep pci_bus_id empty if the struct form
            // is needed. Guarded to avoid misinterpreting the return.
        }
    }

    if (impl_->pfnMemInfo) {
        NvmlMemory mem{};
        if (impl_->pfnMemInfo(handle, &mem) == kNvmlSuccess) {
            out.memory_total = mem.total;
            out.memory_used  = mem.used;
            out.memory_free  = mem.free;
        }
    }

    if (impl_->pfnTemp) {
        unsigned int temp = 0;
        if (impl_->pfnTemp(handle, kNvmlTempGpu, &temp) == kNvmlSuccess) {
            out.temp_celsius   = static_cast<double>(temp);
            out.temp_supported = true;
        }
    }
    if (impl_->pfnTempThresh) {
        unsigned int slowdown = 0, shutdown = 0;
        if (impl_->pfnTempThresh(handle, kNvmlTempThresholdSlowdown, &slowdown) == kNvmlSuccess) {
            out.temp_slowdown_c = static_cast<double>(slowdown);
        }
        if (impl_->pfnTempThresh(handle, kNvmlTempThresholdShutdown, &shutdown) == kNvmlSuccess) {
            out.temp_shutdown_c = static_cast<double>(shutdown);
        }
        out.temp_thresholds_supported = (slowdown > 0 || shutdown > 0);
    }

    if (impl_->pfnPowerUsage) {
        unsigned int mw = 0;
        if (impl_->pfnPowerUsage(handle, &mw) == kNvmlSuccess) {
            out.power_watts     = static_cast<double>(mw) / 1000.0;
            out.power_supported = true;
        }
    }
    if (impl_->pfnPowerLimit) {
        unsigned int mw = 0;
        if (impl_->pfnPowerLimit(handle, &mw) == kNvmlSuccess) {
            out.power_limit_watts     = static_cast<double>(mw) / 1000.0;
            out.power_limit_supported = true;
        }
    }

    if (impl_->pfnUtilization) {
        NvmlUtilization util{};
        if (impl_->pfnUtilization(handle, &util) == kNvmlSuccess) {
            out.gpu_util_percent = static_cast<double>(util.gpu);
            out.mem_util_percent = static_cast<double>(util.memory);
            out.util_supported   = true;
        }
    }

    if (impl_->pfnClock) {
        unsigned int g = 0, sm = 0, mem = 0;
        if (impl_->pfnClock(handle, kNvmlClockGraphics, &g) == kNvmlSuccess) out.clock_graphics_mhz = g;
        if (impl_->pfnClock(handle, kNvmlClockSm, &sm) == kNvmlSuccess)      out.clock_sm_mhz       = sm;
        if (impl_->pfnClock(handle, kNvmlClockMem, &mem) == kNvmlSuccess)    out.clock_mem_mhz      = mem;
        out.clock_supported = (g > 0 || sm > 0 || mem > 0);
    }

    return true;
}

std::vector<NvmlDeviceSnapshot> NvmlLibrary::GetSnapshots() const {
    std::vector<NvmlDeviceSnapshot> result;
    if (!impl_->initialized) return result;
    const int count = GetDeviceCount();
    result.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        NvmlDeviceSnapshot snap;
        if (GetSnapshot(i, snap)) {
            result.push_back(std::move(snap));
        }
    }
    return result;
}

}  // namespace AsyncIO::IO::Hardware
