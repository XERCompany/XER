#include "hw_capability_scanner.h"

#include "nvml_loader.h"

#include <thread>

#if defined(_WIN32)
    #include <windows.h>
    #include <intrin.h>
#elif defined(__linux__)
    #include <sys/sysinfo.h>
#elif defined(__APPLE__)
    #include <sys/sysctl.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    #if __has_include(<cpuid.h>)
        #include <cpuid.h>
    #endif
#endif

namespace AsyncIO::IO::Hardware {

HwCapabilityScanner& HwCapabilityScanner::Instance() {
    static HwCapabilityScanner instance;
    return instance;
}

HwCapabilities HwCapabilityScanner::Scan() {
    HwCapabilities caps{};

    // CPU core count
    caps.cpu_core_count = static_cast<int>(std::thread::hardware_concurrency());

#if defined(_WIN32)
    // SSE4 / AVX2 / AVX-512 via __cpuid.
    int regs[4] = {};
    __cpuid(regs, 1);
    caps.has_sse4 = (regs[2] & (1u << 20)) != 0;   // SSE4.2 in ECX
    __cpuid(regs, 7);
    caps.has_avx2   = (regs[1] & (1u << 5))  != 0;  // AVX2 in EBX
    caps.has_avx512 = (regs[1] & (1u << 16)) != 0;  // AVX-512F in EBX

    // Total RAM via GlobalMemoryStatusEx.
    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        caps.total_ram_bytes = mem_status.ullTotalPhys;
    }
#elif defined(__linux__) && (defined(__x86_64__) || defined(__i386__))
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    #if __has_include(<cpuid.h>)
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        caps.has_sse4 = (ecx & (1u << 20)) != 0;  // SSE4.2
    }
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        caps.has_avx2   = (ebx & (1u << 5))  != 0;
        caps.has_avx512 = (ebx & (1u << 16)) != 0;
    }
    #endif

    // Total RAM via sysinfo.
    struct sysinfo si{};
    if (sysinfo(&si) == 0) {
        caps.total_ram_bytes = static_cast<size_t>(si.totalram) * si.mem_unit;
    }
#elif defined(__APPLE__)
    // macOS: SSE4/AVX2 via sysctl (kept minimal; AVX-512 not present on Apple silicon).
    int has_feature = 0;
    size_t value_size = sizeof(has_feature);
    sysctlbyname("hw.optional.sse4_2", &has_feature, &value_size, nullptr, 0);
    caps.has_sse4 = has_feature != 0;
    value_size = sizeof(has_feature);
    sysctlbyname("hw.optional.avx2_0", &has_feature, &value_size, nullptr, 0);
    caps.has_avx2 = has_feature != 0;
#endif

    // CUDA/NVIDIA presence via the shared NVML loader (works on both
    // Windows and Linux; no compile-time CUDA/NVML dependency).
    auto& nvml = NvmlLibrary::Instance();
    caps.has_cuda = nvml.IsAvailable();
    caps.cuda_device_count = caps.has_cuda ? nvml.GetDeviceCount() : 0;

    return caps;
}

}  // namespace AsyncIO::IO::Hardware
