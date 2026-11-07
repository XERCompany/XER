#include "sync_semaphore_cpu.h"

#include <chrono>
#include <stdexcept>

namespace XER::AsyncIO::IO::Sync {

SyncSemaphoreCpu::SyncSemaphoreCpu(int initial_count, int max_count,
                                   const std::string& name)
    : count_(initial_count), max_count_(max_count), name_(name) {
    if (initial_count < 0) {
        throw std::invalid_argument("SyncSemaphoreCpu: initial_count must be >= 0");
    }
    if (max_count > 0 && initial_count > max_count) {
        throw std::invalid_argument(
            "SyncSemaphoreCpu: initial_count must be <= max_count");
    }
}

SyncSemaphoreCpu::~SyncSemaphoreCpu() = default;

void SyncSemaphoreCpu::Acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return count_ > 0; });
    --count_;
}

bool SyncSemaphoreCpu::TryAcquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ > 0) {
        --count_;
        return true;
    }
    return false;
}

bool SyncSemaphoreCpu::TimedAcquire(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto pred = [this] { return count_ > 0; };
    if (timeout_ms < 0) {
        cv_.wait(lock, pred);
    } else {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred)) {
            return false;  // timed out
        }
    }
    --count_;
    return true;
}

void SyncSemaphoreCpu::Release() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (max_count_ > 0 && count_ >= max_count_) {
            throw std::overflow_error(
                "SyncSemaphoreCpu: Release() would exceed max_count");
        }
        ++count_;
    }
    cv_.notify_one();
}

int SyncSemaphoreCpu::GetCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

}  // namespace XER::AsyncIO::IO::Sync
