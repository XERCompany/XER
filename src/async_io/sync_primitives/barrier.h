#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

namespace XER::AsyncIO::IO::Sync {

class Barrier {
public:
    explicit Barrier(int thread_count, const std::string& name = "");
    ~Barrier();

    // Synchronization
    void Wait();
    bool Reset();

    // Statistics
    const std::string& GetName() const { return name_; }
    int GetThreadCount() const { return thread_count_; }
    int GetWaitingCount() const { return waiting_count_; }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int thread_count_;
    int waiting_count_;
    int generation_;
    std::string name_;
};

}  // namespace XER::AsyncIO::IO::Sync
