#include "cpu_info.h"

#include <fstream>
#include <sstream>

#if defined(_WIN32)
    #include <windows.h>
    #include <intrin.h>
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <mach/mach.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <cpuid.h>
    #include <pthread.h>
    #include <sched.h>
    #include <fstream>
    #include <sstream>
    #include <thread>
    #include <chrono>
#endif

namespace AsyncIO::IO::Hardware {

CPUInfoProvider::CPUInfoProvider()
    : initialized_(false) {
    cpu_info_ = CPUInfo();
}

CPUInfoProvider::~CPUInfoProvider() {}

bool CPUInfoProvider::Initialize() {
#ifdef _WIN32
    // Windows CPU detection
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    
    cpu_info_.logical_cores = sys_info.dwNumberOfProcessors;
    cpu_info_.physical_cores = sys_info.dwNumberOfProcessors / 2;  // Simplified
    
    DetectCPUCapabilities();
    DetectCoreCount();
    
    initialized_ = true;
    return true;
#elif defined(__linux__)
    // Linux CPU detection
    cpu_info_.logical_cores = sysconf(_SC_NPROCESSORS_ONLN);

    // Read physical core count from /proc/cpuinfo
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        int max_core_id = -1;
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("core id", 0) == 0) {
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    try {
                        int cid = std::stoi(line.substr(colon + 1));
                        if (cid > max_core_id) max_core_id = cid;
                    } catch (...) {}
                }
            }
        }
        cpu_info_.physical_cores = (max_core_id >= 0)
            ? (max_core_id + 1)
            : cpu_info_.logical_cores;
    }

    // Read CPU vendor and model from /proc/cpuinfo
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("vendor_id", 0) == 0 && cpu_info_.vendor.empty()) {
                const auto colon = line.find(':');
                if (colon != std::string::npos)
                    cpu_info_.vendor = line.substr(colon + 2);
            } else if (line.rfind("model name", 0) == 0 && cpu_info_.model.empty()) {
                const auto colon = line.find(':');
                if (colon != std::string::npos)
                    cpu_info_.model = line.substr(colon + 2);
            }
            if (!cpu_info_.vendor.empty() && !cpu_info_.model.empty()) break;
        }
    }

    DetectCPUCapabilities();
    DetectCoreCount();
    
    initialized_ = true;
    return true;
#elif defined(__APPLE__)
    size_t value_size = sizeof(int);
    sysctlbyname("hw.logicalcpu", &cpu_info_.logical_cores, &value_size, nullptr, 0);

    value_size = sizeof(int);
    sysctlbyname("hw.physicalcpu", &cpu_info_.physical_cores, &value_size, nullptr, 0);
    if (cpu_info_.logical_cores <= 0) return false;
    if (cpu_info_.physical_cores <= 0) cpu_info_.physical_cores = cpu_info_.logical_cores;

    char cpu_string[256] = {};
    value_size = sizeof(cpu_string);
    if (sysctlbyname("machdep.cpu.vendor", cpu_string, &value_size, nullptr, 0) == 0)
        cpu_info_.vendor = cpu_string;

    value_size = sizeof(cpu_string);
    if (sysctlbyname("machdep.cpu.brand_string", cpu_string, &value_size, nullptr, 0) == 0)
        cpu_info_.model = cpu_string;
    if (cpu_info_.model.empty()) {
        value_size = sizeof(cpu_string);
        if (sysctlbyname("hw.model", cpu_string, &value_size, nullptr, 0) == 0)
            cpu_info_.model = cpu_string;
    }

    DetectCPUCapabilities();
    DetectCoreCount();
    initialized_ = true;
    return true;
#else
    return false;
#endif
}

bool CPUInfoProvider::IsInitialized() const {
    return initialized_;
}

std::vector<ProcessorCore> CPUInfoProvider::GetCores() const {
    return cores_;
}

uint64_t CPUInfoProvider::GetTotalMemoryMB() const {
#ifdef _WIN32
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    return mem_status.ullTotalPhys / (1024 * 1024);
#elif defined(__linux__)
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return (pages * page_size) / (1024 * 1024);
#elif defined(__APPLE__)
    uint64_t memory_bytes = 0;
    size_t value_size = sizeof(memory_bytes);
    if (sysctlbyname("hw.memsize", &memory_bytes, &value_size, nullptr, 0) != 0) return 0;
    return memory_bytes / (1024 * 1024);
#else
    return 0;
#endif
}

uint64_t CPUInfoProvider::GetAvailableMemoryMB() const {
#ifdef _WIN32
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    return mem_status.ullAvailPhys / (1024 * 1024);
#elif defined(__linux__)
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return (avail_pages * page_size) / (1024 * 1024);
#elif defined(__APPLE__)
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    vm_statistics64_data_t vm_stats = {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_page_size(host, &page_size) != KERN_SUCCESS ||
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast< host_info64_t>(&vm_stats), &count) != KERN_SUCCESS) {
        return 0;
    }
    return (static_cast<uint64_t>(vm_stats.free_count + vm_stats.inactive_count) * page_size) / (1024 * 1024);
#else
    return 0;
#endif
}

bool CPUInfoProvider::SetThreadAffinity(int core_id) {
#ifdef _WIN32
    HANDLE current_thread = GetCurrentThread();
    DWORD_PTR mask = 1ULL << core_id;
    return (SetThreadAffinityMask(current_thread, mask) != 0);
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0;
#else
    return false;
#endif
}

int CPUInfoProvider::GetCurrentCore() const {
#ifdef _WIN32
    return GetCurrentProcessorNumber();
#elif defined(__linux__)
    return sched_getcpu();
#else
    return 0;
#endif
}

// Reads a cpu row from /proc/stat; returns false on failure.
// Fields: user nice system idle iowait irq softirq steal
static bool ReadProcStatCpuLine(const std::string& tag,
                                 uint64_t& idle_out,
                                 uint64_t& total_out) {
    std::ifstream stat("/proc/stat");
    if (!stat.is_open()) return false;
    std::string line;
    while (std::getline(stat, line)) {
        if (line.rfind(tag, 0) != 0) continue;
        std::istringstream iss(line);
        std::string cpu_label;
        iss >> cpu_label;
        uint64_t user, nice, system, idle, iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (!(iss >> user >> nice >> system >> idle)) return false;
        iss >> iowait >> irq >> softirq >> steal;
        idle_out  = idle + iowait;
        total_out = user + nice + system + idle + iowait + irq + softirq + steal;
        return true;
    }
    return false;
}

double CPUInfoProvider::GetCPUUsagePercent() const {
#ifdef __linux__
    uint64_t idle1 = 0, total1 = 0, idle2 = 0, total2 = 0;
    if (!ReadProcStatCpuLine("cpu ", idle1, total1)) return 0.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!ReadProcStatCpuLine("cpu ", idle2, total2)) return 0.0;
    uint64_t d_total = total2 - total1;
    uint64_t d_idle  = idle2  - idle1;
    if (d_total == 0) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total));
#else
    return 0.0;
#endif
}

double CPUInfoProvider::GetCoreUsagePercent(int core_id) const {
#ifdef __linux__
    const std::string tag = "cpu" + std::to_string(core_id) + " ";
    uint64_t idle1 = 0, total1 = 0, idle2 = 0, total2 = 0;
    if (!ReadProcStatCpuLine(tag, idle1, total1)) return 0.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!ReadProcStatCpuLine(tag, idle2, total2)) return 0.0;
    uint64_t d_total = total2 - total1;
    uint64_t d_idle  = idle2  - idle1;
    if (d_total == 0) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total));
#else
    return 0.0;
#endif
}

std::string CPUInfoProvider::GetCPUDescription() const {
    std::string desc = cpu_info_.vendor + " " + cpu_info_.model;
    desc += " (" + std::to_string(cpu_info_.physical_cores) + "P/" + 
            std::to_string(cpu_info_.logical_cores) + "L)";
    return desc;
}

void CPUInfoProvider::DetectCPUCapabilities() {
#if defined(_WIN32)
    int regs[4];
    __cpuid(regs, 1);
    // ECX register bits for SSE, AVX detection
    cpu_info_.supports_sse = (regs[2] & 0x1000000) != 0;   // SSE2 in EDX
    cpu_info_.supports_avx = (regs[2] & 0x10000000) != 0;  // AVX in ECX
    __cpuid(regs, 7);
    cpu_info_.supports_avx2 = (regs[1] & 0x20) != 0;       // AVX2 in EBX
    cpu_info_.supports_avx512 = (regs[1] & 0x10000) != 0;  // AVX-512F in EBX
#elif defined(__linux__) && defined(__x86_64__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        cpu_info_.supports_sse   = (edx & (1u << 26)) != 0;  // SSE2
        cpu_info_.supports_avx   = (ecx & (1u << 28)) != 0;  // AVX
    }
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        cpu_info_.supports_avx2   = (ebx & (1u << 5))  != 0;  // AVX2
        cpu_info_.supports_avx512 = (ebx & (1u << 16)) != 0;  // AVX-512F
    }
#endif
}

void CPUInfoProvider::DetectCoreCount() {
    for (int i = 0; i < cpu_info_.logical_cores; ++i) {
        ProcessorCore core;
        core.core_id = i / 2;
        core.logical_id = i;
        core.frequency_mhz = 0;
        core.is_active = true;
        cores_.push_back(core);
    }
}

}  // namespace AsyncIO::IO::Hardware
