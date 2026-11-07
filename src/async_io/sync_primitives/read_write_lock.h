#pragma once

#include <shared_mutex>
#include <string>

namespace XER::AsyncIO::IO::Sync {

class ReadWriteLock {
public:
    ReadWriteLock(const std::string& name = "");
    ~ReadWriteLock();

    // Read lock
    void LockRead();
    bool TryLockRead();
    void UnlockRead();

    // Write lock
    void LockWrite();
    bool TryLockWrite();
    void UnlockWrite();

    // RAII locks
    class ReadLock {
    public:
        explicit ReadLock(ReadWriteLock& lock) : lock_(lock) {
            lock_.LockRead();
        }
        ~ReadLock() {
            lock_.UnlockRead();
        }
        ReadLock(const ReadLock&) = delete;
        ReadLock& operator=(const ReadLock&) = delete;
    private:
        ReadWriteLock& lock_;
    };

    class WriteLock {
    public:
        explicit WriteLock(ReadWriteLock& lock) : lock_(lock) {
            lock_.LockWrite();
        }
        ~WriteLock() {
            lock_.UnlockWrite();
        }
        WriteLock(const WriteLock&) = delete;
        WriteLock& operator=(const WriteLock&) = delete;
    private:
        ReadWriteLock& lock_;
    };

    // Statistics
    const std::string& GetName() const { return name_; }
    uint64_t GetReadLockCount() const { return read_lock_count_; }
    uint64_t GetWriteLockCount() const { return write_lock_count_; }

private:
    mutable std::shared_mutex rwlock_;
    std::string name_;
    uint64_t read_lock_count_;
    uint64_t write_lock_count_;
};

}  // namespace XER::AsyncIO::IO::Sync
