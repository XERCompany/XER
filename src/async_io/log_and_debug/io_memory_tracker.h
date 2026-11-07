#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <mutex>
#include <vector>
#include <memory>

namespace XER::AsyncIO::IO::LogDebug {

struct AllocationInfo {
    uint64_t address;
    size_t size;
    std::string tag;
    int64_t timestamp_ms;
};

class MemoryTracker {
public:
    MemoryTracker();
    ~MemoryTracker();

    // Tracking control
    void Enable();
    void Disable();
    bool IsEnabled() const { return enabled_; }

    // Memory tracking
    void TrackAllocation(void* ptr, size_t size, const std::string& tag);
    void TrackDeallocation(void* ptr);

    // Statistics
    uint64_t GetTotalAllocations() const;
    uint64_t GetTotalDeallocations() const;
    uint64_t GetCurrentlyAllocatedMB() const;
    uint64_t GetPeakAllocationMB() const;

    // Breakdown by tag
    std::map<std::string, uint64_t> GetAllocationsByTag() const;
    std::string GetAllocationReport() const;

    // Leak detection
    std::vector<AllocationInfo> GetLiveAllocations() const;
    bool DetectLeaks(uint64_t threshold_mb = 1) const;

private:
    mutable std::mutex mutex_;
    bool enabled_;
    std::map<uint64_t, AllocationInfo> allocations_;
    uint64_t total_allocations_;
    uint64_t total_deallocations_;
    uint64_t peak_allocated_mb_;

    uint64_t GetCurrentAllocatedBytes() const;
};

}  // namespace XER::AsyncIO::IO::LogDebug
