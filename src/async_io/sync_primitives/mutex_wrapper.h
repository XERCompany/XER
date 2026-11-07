#pragma once

#include <mutex>
#include <string>
#include <chrono>

namespace XER::AsyncIO::IO::Sync {

class MutexWrapper {
public:
    MutexWrapper(const std::string& name = "");
    ~MutexWrapper();

    // Locking
    void Lock();
    bool TryLock();
    bool TryLockFor(const std::chrono::milliseconds& timeout);
    void Unlock();

    // RAII lock
    class ScopedLock {
    public:
        explicit ScopedLock(MutexWrapper& mutex) : mutex_(mutex) {
            mutex_.Lock();
        }
        
        ~ScopedLock() {
            mutex_.Unlock();
        }

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        MutexWrapper& mutex_;
    };

    // Statistics
    const std::string& GetName() const { return name_; }
    uint64_t GetLockCount() const { return lock_count_; }
    uint64_t GetContentionCount() const { return contention_count_; }

private:
    std::mutex mutex_;
    std::string name_;
    uint64_t lock_count_;
    uint64_t contention_count_;
};

}  // namespace XER::AsyncIO::IO::Sync
