#include "read_write_lock.h"

namespace XER::AsyncIO::IO::Sync {

ReadWriteLock::ReadWriteLock(const std::string& name)
    : name_(name), read_lock_count_(0), write_lock_count_(0) {}

ReadWriteLock::~ReadWriteLock() {}

void ReadWriteLock::LockRead() {
    rwlock_.lock_shared();
    read_lock_count_++;
}

bool ReadWriteLock::TryLockRead() {
    bool success = rwlock_.try_lock_shared();
    if (success) {
        read_lock_count_++;
    }
    return success;
}

void ReadWriteLock::UnlockRead() {
    rwlock_.unlock_shared();
}

void ReadWriteLock::LockWrite() {
    rwlock_.lock();
    write_lock_count_++;
}

bool ReadWriteLock::TryLockWrite() {
    bool success = rwlock_.try_lock();
    if (success) {
        write_lock_count_++;
    }
    return success;
}

void ReadWriteLock::UnlockWrite() {
    rwlock_.unlock();
}

}  // namespace XER::AsyncIO::IO::Sync
