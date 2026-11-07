#include "memory_monitor.h"

#include "nvml_loader.h"

#ifdef _WIN32
    #include <windows.h>
#elif defined(__linux__)
    #include <unistd.h>
#endif

namespace XER::AsyncIO::IO::Hardware {

MemoryMonitor::MemoryMonitor()
    : is_monitoring_(false), peak_memory_mb_(0), tracked_allocation_mb_(0),
      low_memory_threshold_(0.9) {}

MemoryMonitor::~MemoryMonitor() {
    StopMonitoring();
}

bool MemoryMonitor::StartMonitoring() {
    is_monitoring_ = true;
    start_time_ = std::chrono::steady_clock::now();
    peak_memory_mb_ = 0;
    return true;
}

bool MemoryMonitor::StopMonitoring() {
    is_monitoring_ = false;
    return true;
}

MemoryStats MemoryMonitor::GetMemoryStats() const {
    return QuerySystemMemory();
}

MemoryStats MemoryMonitor::GetGPUMemoryStats(int device_id) const {
    MemoryStats stats = {0, 0, 0, 0.0, 0};

    auto& nvml = NvmlLibrary::Instance();
    if (!nvml.IsAvailable()) return stats;

    NvmlDeviceSnapshot snap;
    if (!nvml.GetSnapshot(device_id, snap)) return stats;

    stats.total_mb = snap.memory_total / (1024ULL * 1024ULL);
    stats.free_mb  = snap.memory_free  / (1024ULL * 1024ULL);
    stats.used_mb  = snap.memory_used  / (1024ULL * 1024ULL);
    stats.usage_percent = (snap.memory_total > 0)
        ? static_cast<double>(snap.memory_used) / static_cast<double>(snap.memory_total)
        : 0.0;
    stats.peak_usage_mb = stats.used_mb;
    return stats;
}

void MemoryMonitor::ResetPeakMemory() {
    peak_memory_mb_ = 0;
}

void MemoryMonitor::SetLowMemoryThreshold(double percent) {
    low_memory_threshold_ = percent;
}

bool MemoryMonitor::IsLowMemory() const {
    MemoryStats stats = QuerySystemMemory();
    return (stats.usage_percent >= low_memory_threshold_);
}

void MemoryMonitor::RecordAllocation(size_t bytes) {
    uint64_t mb = bytes / (1024 * 1024);
    tracked_allocation_mb_ += mb;

    if (tracked_allocation_mb_ > peak_memory_mb_) {
        peak_memory_mb_ = tracked_allocation_mb_;
    }
}

void MemoryMonitor::RecordDeallocation(size_t bytes) {
    uint64_t mb = bytes / (1024 * 1024);
    if (tracked_allocation_mb_ >= mb) {
        tracked_allocation_mb_ -= mb;
    }
}

uint64_t MemoryMonitor::GetTrackedAllocationMB() const {
    return tracked_allocation_mb_;
}

MemoryStats MemoryMonitor::QuerySystemMemory() const {
    MemoryStats stats = {0, 0, 0, 0.0, 0};

#ifdef _WIN32
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);

    if (GlobalMemoryStatusEx(&mem_status)) {
        stats.total_mb = mem_status.ullTotalPhys / (1024 * 1024);
        stats.free_mb = mem_status.ullAvailPhys / (1024 * 1024);
        stats.used_mb = stats.total_mb - stats.free_mb;
        stats.usage_percent = static_cast<double>(stats.used_mb) / stats.total_mb;
        stats.peak_usage_mb = peak_memory_mb_;
    }
#elif defined(__linux__)
    long total_pages = sysconf(_SC_PHYS_PAGES);
    long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);

    stats.total_mb = (total_pages * page_size) / (1024 * 1024);
    stats.free_mb = (avail_pages * page_size) / (1024 * 1024);
    stats.used_mb = stats.total_mb - stats.free_mb;
    stats.usage_percent = static_cast<double>(stats.used_mb) / stats.total_mb;
    stats.peak_usage_mb = peak_memory_mb_;
#endif

    return stats;
}

}  // namespace XER::AsyncIO::IO::Hardware
