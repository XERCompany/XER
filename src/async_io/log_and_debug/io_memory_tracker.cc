#include "io_memory_tracker.h"

#include <chrono>

namespace XER::AsyncIO::IO::LogDebug {

MemoryTracker::MemoryTracker()
    : enabled_(true), total_allocations_(0), total_deallocations_(0), peak_allocated_mb_(0) {}

MemoryTracker::~MemoryTracker() {
    std::lock_guard lock(mutex_);
    allocations_.clear();
}

void MemoryTracker::Enable() {
    std::lock_guard lock(mutex_);
    enabled_ = true;
}

void MemoryTracker::Disable() {
    std::lock_guard lock(mutex_);
    enabled_ = false;
}

void MemoryTracker::TrackAllocation(void* ptr, size_t size, const std::string& tag) {
    std::lock_guard lock(mutex_);
    if (!enabled_ || !ptr) return;

    AllocationInfo info;
    info.address = reinterpret_cast<std::uint64_t>(ptr);
    info.size = size;
    info.tag = tag;
    info.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    allocations_[info.address] = info;
    total_allocations_++;

    std::uint64_t current_bytes = 0;
    for (const auto& pair : allocations_) {
        current_bytes += pair.second.size;
    }
    const std::uint64_t current_mb = current_bytes / (1024 * 1024);
    if (current_mb > peak_allocated_mb_) {
        peak_allocated_mb_ = current_mb;
    }
}

void MemoryTracker::TrackDeallocation(void* ptr) {
    std::lock_guard lock(mutex_);
    if (!enabled_ || !ptr) return;

    std::uint64_t address = reinterpret_cast<std::uint64_t>(ptr);
    allocations_.erase(address);
    total_deallocations_++;
}

uint64_t MemoryTracker::GetTotalAllocations() const {
    std::lock_guard lock(mutex_);
    return total_allocations_;
}

uint64_t MemoryTracker::GetTotalDeallocations() const {
    std::lock_guard lock(mutex_);
    return total_deallocations_;
}

uint64_t MemoryTracker::GetCurrentlyAllocatedMB() const {
    return GetCurrentAllocatedBytes() / (1024 * 1024);
}

uint64_t MemoryTracker::GetPeakAllocationMB() const {
    std::lock_guard lock(mutex_);
    return peak_allocated_mb_;
}

std::map<std::string, uint64_t> MemoryTracker::GetAllocationsByTag() const {
    std::lock_guard lock(mutex_);
    std::map<std::string, uint64_t> breakdown;
    
    for (const auto& pair : allocations_) {
        const auto& info = pair.second;
        breakdown[info.tag] += info.size;
    }
    
    return breakdown;
}

std::string MemoryTracker::GetAllocationReport() const {
    std::lock_guard lock(mutex_);
    std::string report;
    report += "=== Memory Allocation Report ===\n";
    report += "Total Allocations: " + std::to_string(total_allocations_) + "\n";
    report += "Total Deallocations: " + std::to_string(total_deallocations_) + "\n";
    std::uint64_t current_bytes = 0;
    std::map<std::string, std::uint64_t> breakdown;
    for (const auto& pair : allocations_) {
        current_bytes += pair.second.size;
        breakdown[pair.second.tag] += pair.second.size;
    }
    report += "Currently Allocated: " + std::to_string(current_bytes / (1024 * 1024)) + " MB\n";
    report += "Peak Allocation: " + std::to_string(peak_allocated_mb_) + " MB\n\n";

    report += "Breakdown by Tag:\n";
    for (const auto& pair : breakdown) {
        report += "  " + pair.first + ": " + std::to_string(pair.second / 1024) + " KB\n";
    }

    return report;
}

std::vector<AllocationInfo> MemoryTracker::GetLiveAllocations() const {
    std::lock_guard lock(mutex_);
    std::vector<AllocationInfo> live;
    for (const auto& pair : allocations_) {
        live.push_back(pair.second);
    }
    return live;
}

bool MemoryTracker::DetectLeaks(uint64_t threshold_mb) const {
    return GetCurrentlyAllocatedMB() >= threshold_mb;
}

uint64_t MemoryTracker::GetCurrentAllocatedBytes() const {
    std::lock_guard lock(mutex_);
    std::uint64_t total = 0;
    for (const auto& pair : allocations_) {
        total += pair.second.size;
    }
    return total;
}

}  // namespace XER::AsyncIO::IO::LogDebug
