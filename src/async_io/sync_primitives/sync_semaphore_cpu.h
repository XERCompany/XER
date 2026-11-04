#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <cstdint>

namespace AsyncIO::IO::Sync {

// Counting semaphore with an optional maximum count.
//
// Thread-safety: all public methods are safe to call from any thread.
class SyncSemaphoreCpu {
public:
    // initial_count must be >= 0. max_count == 0 means "no upper bound"
    // (unbounded counting semaphore, legacy behavior). If max_count > 0,
    // initial_count must be <= max_count and Release() beyond max_count
    // throws std::overflow_error.
    explicit SyncSemaphoreCpu(int initial_count, int max_count = 0,
                              const std::string& name = "");
    ~SyncSemaphoreCpu();

    // Block until count > 0, then decrement.
    void Acquire();

    // Try to decrement without blocking. Returns true if acquired.
    bool TryAcquire();

    // Wait up to timeout_ms for count > 0, then decrement. Returns true if
    // acquired, false on timeout. timeout_ms < 0 means wait forever.
    bool TimedAcquire(int timeout_ms);

    // Increment. If max_count > 0 and count would exceed it, throws
    // std::overflow_error (bounded semaphore). Wakes one waiter.
    void Release();

    int GetCount() const;
    int GetMaxCount() const { return max_count_; }
    const std::string& GetName() const { return name_; }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
    int max_count_;  // 0 = unbounded
    std::string name_;
};

}  // namespace AsyncIO::IO::Sync
