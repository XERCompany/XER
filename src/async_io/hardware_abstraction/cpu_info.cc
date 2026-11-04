#include "cpu_info.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

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
#endif

namespace AsyncIO::IO::Hardware {

namespace {

// ---- FileTime helpers (Windows) -------------------------------------------
uint64_t FileTimeToU64(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

#if defined(_WIN32)
// SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX iteration helper.
// RelationAll is not always defined in older SDKs, so we enumerate each
// relation type separately to be safe.
void CountTopologyWin(int& physical_cores, int& logical_cores,
                      int& numa_nodes, int& sockets,
                      uint64_t& l1, uint64_t& l2, uint64_t& l3) {
    physical_cores = logical_cores = numa_nodes = sockets = 0;
    l1 = l2 = l3 = 0;

    const LOGICAL_PROCESSOR_RELATIONSHIP relations[] = {
        RelationProcessorCore,
        RelationProcessorPackage,  // sockets (Win7+)
        RelationNumaNode,
        RelationCache,
    };

    for (auto rel : relations) {
        DWORD len = 0;
        if (!GetLogicalProcessorInformationEx(rel, nullptr, &len)) {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;
        }
        if (len == 0) continue;

        std::vector<uint8_t> buffer(len);
        if (!GetLogicalProcessorInformationEx(rel, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &len)) {
            continue;
        }

        DWORD offset = 0;
        while (offset < len) {
            auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
            switch (entry->Relationship) {
                case RelationProcessorCore: {
                    ++physical_cores;
                    // Count logical processors (bits) across all groups.
                    for (WORD g = 0; g < entry->Processor.GroupCount; ++g) {
                        const auto& mask = entry->Processor.GroupMask[g].Mask;
                        logical_cores += static_cast<int>(__popcnt64(mask));
                    }
                    break;
                }
                case RelationProcessorPackage: {
                    ++sockets;
                    break;
                }
                case RelationNumaNode: {
                    ++numa_nodes;
                    break;
                }
                case RelationCache: {
                    const uint64_t sz = static_cast<uint64_t>(entry->Cache.CacheSize);
                    const BYTE level = entry->Cache.Level;
                    if (level == 1 && sz > l1) l1 = sz;
                    else if (level == 2 && sz > l2) l2 = sz;
                    else if (level == 3 && sz > l3) l3 = sz;
                    break;
                }
                default:
                    break;
            }
            offset += entry->Size;
            if (entry->Size == 0) break;
        }
    }
}

// Read CPU vendor and brand string via __cpuid.
void ReadCpuidBrand(std::string& vendor, std::string& model) {
    int regs[4] = {};
    __cpuid(regs, 0);
    // Vendor: ebx, edx, ecx
    char vendor_buf[13] = {};
    std::memcpy(vendor_buf + 0, &regs[1], 4);
    std::memcpy(vendor_buf + 4, &regs[3], 4);
    std::memcpy(vendor_buf + 8, &regs[2], 4);
    vendor = vendor_buf;

    // Brand string: 0x80000002..0x80000004 (each yields 16 bytes)
    char brand[49] = {};
    __cpuid(regs, static_cast<int>(0x80000002));
    std::memcpy(brand + 0,  regs,      16);
    __cpuid(regs, static_cast<int>(0x80000003));
    std::memcpy(brand + 16, regs,      16);
    __cpuid(regs, static_cast<int>(0x80000004));
    std::memcpy(brand + 32, regs,      16);
    model = brand;
    // Trim leading spaces.
    size_t start = model.find_first_not_of(" \t");
    if (start != std::string::npos && start > 0) model.erase(0, start);
}

#endif  // _WIN32

}  // namespace

CPUInfoProvider::CPUInfoProvider()
    : initialized_(false) {
    cpu_info_ = CPUInfo();
}

CPUInfoProvider::~CPUInfoProvider() {}

bool CPUInfoProvider::Initialize() {
#ifdef _WIN32
    int physical = 0, logical = 0, numa = 0, sock = 0;
    uint64_t l1 = 0, l2 = 0, l3 = 0;
    CountTopologyWin(physical, logical, numa, sock, l1, l2, l3);

    ReadCpuidBrand(cpu_info_.vendor, cpu_info_.model);

    // Fall back to SYSTEM_INFO if topology enumeration found nothing.
    if (logical == 0) {
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        logical = static_cast<int>(sys_info.dwNumberOfProcessors);
    }
    if (physical == 0) physical = logical / 2;
    if (sock == 0) sock = 1;

    cpu_info_.logical_cores  = logical;
    cpu_info_.physical_cores = physical;
    cpu_info_.l1_cache_kb = l1 / 1024;
    cpu_info_.l2_cache_kb = l2 / 1024;
    cpu_info_.l3_cache_kb = l3 / 1024;

    DetectCPUCapabilities();
    DetectCoreCount();

    initialized_ = true;
    return true;
#elif defined(__linux__)
    cpu_info_.logical_cores = sysconf(_SC_NPROCESSORS_ONLN);

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

    // Cache sizes from /sys/devices/system/cpu/cpu0/cache/indexN/size
    for (int idx = 0; idx < 8; ++idx) {
        const std::string base = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(idx);
        std::ifstream level_f(base + "/level");
        if (!level_f.is_open()) break;
        int level = 0;
        level_f >> level;
        std::ifstream size_f(base + "/size");
        if (!size_f.is_open()) continue;
        // size is like "32K", "256K", "12288K", or "32M"
        std::string size_str;
        size_f >> size_str;
        uint64_t kb = 0;
        char unit = 'K';
        if (!size_str.empty()) {
            try {
                double v = std::stod(size_str);
                unit = size_str.back();
                if (unit == 'K')      kb = static_cast<uint64_t>(v);
                else if (unit == 'M') kb = static_cast<uint64_t>(v * 1024);
                else if (unit == 'G') kb = static_cast<uint64_t>(v * 1024 * 1024);
                else                  kb = static_cast<uint64_t>(v / 1024);
            } catch (...) {}
        }
        if (level == 1)      cpu_info_.l1_cache_kb = kb;
        else if (level == 2) cpu_info_.l2_cache_kb = kb;
        else if (level == 3) cpu_info_.l3_cache_kb = kb;
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

    // L1/L2/L3 on macOS
    uint64_t l1 = 0, l2 = 0, l3 = 0;
    value_size = sizeof(l1);
    sysctlbyname("hw.l1icachesize", &l1, &value_size, nullptr, 0);  // instruction
    value_size = sizeof(l1);
    sysctlbyname("hw.l1dcachesize", &l1, &value_size, nullptr, 0);
    cpu_info_.l1_cache_kb = l1 / 1024;
    value_size = sizeof(l2);
    sysctlbyname("hw.l2cachesize", &l2, &value_size, nullptr, 0);
    cpu_info_.l2_cache_kb = l2 / 1024;
    value_size = sizeof(l3);
    sysctlbyname("hw.l3cachesize", &l3, &value_size, nullptr, 0);
    cpu_info_.l3_cache_kb = l3 / 1024;

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
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm_stats), &count) != KERN_SUCCESS) {
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
    return static_cast<int>(GetCurrentProcessorNumber());
#elif defined(__linux__)
    return sched_getcpu();
#else
    return 0;
#endif
}

// Reads a cpu row from /proc/stat; returns false on failure.
// Linux-only; unused on other platforms.
#if defined(__linux__)
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
#endif  // __linux__

double CPUInfoProvider::GetCPUUsagePercent() const {
#if defined(_WIN32)
    FILETIME idle1, kernel1, user1;
    FILETIME idle2, kernel2, user2;
    if (!GetSystemTimes(&idle1, &kernel1, &user1)) return 0.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!GetSystemTimes(&idle2, &kernel2, &user2)) return 0.0;

    const uint64_t idle_d   = FileTimeToU64(idle2)   - FileTimeToU64(idle1);
    const uint64_t kernel_d = FileTimeToU64(kernel2) - FileTimeToU64(kernel1);
    const uint64_t user_d   = FileTimeToU64(user2)   - FileTimeToU64(user1);
    const uint64_t total_d = kernel_d + user_d;  // kernel includes idle
    if (total_d == 0) return 0.0;
    if (idle_d > total_d) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(idle_d) / static_cast<double>(total_d));
#elif defined(__linux__)
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
#if defined(_WIN32)
    // Per-core CPU times via NtQuerySystemInformation(SystemProcessorTimes).
    // SystemProcessorTimes = 8. Returns an array of SYSTEM_PROCESSOR_TIMES,
    // one per logical processor. Loaded dynamically from ntdll to avoid a
    // hard dependency on ntdll.lib and the (semi-)undocumented structs.
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0.0;
    using PfnNtQuery = LONG (WINAPI*)(ULONG, PVOID, ULONG, PULONG);
    auto pfn = reinterpret_cast<PfnNtQuery>(GetProcAddress(ntdll, "NtQuerySystemInformation"));
    if (!pfn) return 0.0;

    struct SystemProcessorTimesEntry {
        int64_t IdleTime;
        int64_t KernelTime;
        int64_t UserTime;
        int64_t DpcTime;
        int64_t InterruptTime;
        uint32_t InterruptCount;
    };

    auto read_all = [&](std::vector<SystemProcessorTimesEntry>& out) -> bool {
        ULONG len = 0;
        if (pfn(8, nullptr, 0, &len) != 0) {  // STATUS_INFO_LENGTH_MISMATCH expected
            // fall through
        }
        if (len == 0) len = 4096;
        std::vector<uint8_t> buf(len);
        ULONG ret = 0;
        LONG status = pfn(8, buf.data(), static_cast<ULONG>(buf.size()), &ret);
        if (status != 0) return false;
        const size_t entry_size = sizeof(SystemProcessorTimesEntry);
        const size_t count = ret / entry_size;
        out.resize(count);
        std::memcpy(out.data(), buf.data(), count * entry_size);
        return true;
    };

    std::vector<SystemProcessorTimesEntry> a, b;
    if (!read_all(a)) return 0.0;
    if (core_id < 0 || core_id >= static_cast<int>(a.size())) return 0.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!read_all(b)) return 0.0;
    if (core_id >= static_cast<int>(b.size())) return 0.0;

    const int64_t idle_d  = b[core_id].IdleTime   - a[core_id].IdleTime;
    const int64_t kern_d  = b[core_id].KernelTime - a[core_id].KernelTime;
    const int64_t user_d  = b[core_id].UserTime   - a[core_id].UserTime;
    const int64_t total_d = kern_d + user_d;  // kernel includes idle
    if (total_d <= 0) return 0.0;
    if (idle_d > total_d) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(idle_d) / static_cast<double>(total_d));
#elif defined(__linux__)
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
    cpu_info_.supports_sse = (regs[3] & 0x4000000) != 0;   // SSE2 in EDX (regs[3])
    cpu_info_.supports_avx = (regs[2] & 0x10000000) != 0;  // AVX in ECX (regs[2])
    __cpuid(regs, 7);
    cpu_info_.supports_avx2 = (regs[1] & 0x20) != 0;       // AVX2 in EBX (regs[1])
    cpu_info_.supports_avx512 = (regs[1] & 0x10000) != 0;  // AVX-512F in EBX (regs[1])
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
