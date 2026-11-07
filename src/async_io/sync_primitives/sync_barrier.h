#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

namespace XER::AsyncIO::IO::Sync {

class SyncBarrier {
public:
    explicit SyncBarrier(int count, const std::string& name = "");
    ~SyncBarrier();

    // Phase synchronization
    void Arrive();
    void WaitPhase(int phase);
    bool Reset();

    // Statistics
    const std::string& GetName() const { return name_; }
    int GetCount() const { return count_; }
    int GetWaiting() const { return waiting_; }
    int GetPhase() const { return phase_; }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
    int waiting_;
    int phase_;
    std::string name_;
};

}  // namespace XER::AsyncIO::IO::Sync
