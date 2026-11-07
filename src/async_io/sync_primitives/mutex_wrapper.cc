#include "mutex_wrapper.h"

#include <thread>

namespace XER::AsyncIO::IO::Sync {

MutexWrapper::MutexWrapper(const std::string& name)
    : name_(name), lock_count_(0), contention_count_(0) {}

MutexWrapper::~MutexWrapper() {}

void MutexWrapper::Lock() {
    if (!TryLock()) {
        contention_count_++;
        mutex_.lock();
    }
    lock_count_++;
}

bool MutexWrapper::TryLock() {
    bool success = mutex_.try_lock();
    if (success) {
        lock_count_++;
    }
    return success;
}

bool MutexWrapper::TryLockFor(const std::chrono::milliseconds& timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() <= deadline) {
        if (mutex_.try_lock()) {
            lock_count_++;
            return true;
        }
        std::this_thread::yield();
    }

    contention_count_++;
    return false;
}

void MutexWrapper::Unlock() {
    mutex_.unlock();
}

}  // namespace XER::AsyncIO::IO::Sync
