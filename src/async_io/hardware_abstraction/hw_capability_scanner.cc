#include "hw_capability_scanner.h"

#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#if has_include(<cpuid.h>)
#  include <cpuid.h>
#endif
#endif

#if defined(__linux__)
#  include <dlfcn.h>
#  include <sys/sysinfo.h>
#endif

namespace AsyncIO::IO::HW {

HwCapabilityScanner& HwCapabilityScanner::Instance() {
    static HwCapabilityScanner instance;
    return instance;
}

// ---------------------------------------------------------------------------
// NVML runtime detection via dlopen — no compile-time CUDA dependency
// ---------------------------------------------------------------------------
namespace {

#if defined(__linux__)

// Minimal NVML typedefs we need (matches nvml.h ABI)
using nvmlReturn_t = int;
using nvmlDevice_t = void*;
constexpr nvmlReturn_t kNvmlSuccess = 0;

using PfnNvmlInit     = nvmlReturn_t (*)();
using PfnNvmlShutdown = nvmlReturn_t (*)();
using PfnDeviceCount  = nvmlReturn_t (*)(unsigned int*);

struct NvmlCudaResult {
    bool has_cuda       = false;
    int  device_count   = 0;
};

NvmlCudaResult DetectCudaViaNvml() {
    void* lib = ::dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) {
        // Try unversioned name as fallback
        lib = ::dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (!lib) {
        return {};
    }

    auto nvml_init =
        reinterpret_cast<PfnNvmlInit>(::dlsym(lib, "nvmlInit_v2"));
    if (!nvml_init) {
        nvml_init = reinterpret_cast<PfnNvmlInit>(::dlsym(lib, "nvmlInit"));
    }
    auto nvml_device_count =
        reinterpret_cast<PfnDeviceCount>(::dlsym(lib, "nvmlDeviceGetCount_v2"));
    if (!nvml_device_count) {
        nvml_device_count =
            reinterpret_cast<PfnDeviceCount>(::dlsym(lib, "nvmlDeviceGetCount"));
    }
    auto nvml_shutdown =
        reinterpret_cast<PfnNvmlShutdown>(::dlsym(lib, "nvmlShutdown"));

    NvmlCudaResult result;

    if (nvml_init && nvml_init() == kNvmlSuccess) {
        if (nvml_device_count) {
            unsigned int count = 0;
            if (nvml_device_count(&count) == kNvmlSuccess) {
                result.has_cuda     = (count > 0);
                result.device_count = static_cast<int>(count);
            }
        }
        if (nvml_shutdown) {
            nvml_shutdown();
        }
    }

    ::dlclose(lib);
    return result;
}

#endif  // __linux__

}  // namespace

HwCapabilities HwCapabilityScanner::Scan() {
    HwCapabilities caps{};

    // CPU core count
    caps.cpu_core_count = static_cast<int>(std::thread::hardware_concurrency());

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    // SSE4 / AVX2 detection via CPUID
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
#if has_include(<cpuid.h>)
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        caps.has_sse4 = (ecx & bit_SSE4_2) != 0;
    }
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        caps.has_avx2 = (ebx & bit_AVX2) != 0;
    }
#endif
#endif

    // Total RAM
#if defined(__linux__)
    struct sysinfo si{};
    if (sysinfo(&si) == 0) {
        caps.total_ram_bytes = static_cast<size_t>(si.totalram) * si.mem_unit;
    }

    // CUDA via NVML (dlopen — no link-time dependency on libcuda/libnvml)
    const auto nvml = DetectCudaViaNvml();
    caps.has_cuda        = nvml.has_cuda;
    caps.cuda_device_count = nvml.device_count;
#else
    caps.has_cuda        = false;
    caps.cuda_device_count = 0;
#endif

    return caps;
}

}  // namespace AsyncIO::IO::HW
