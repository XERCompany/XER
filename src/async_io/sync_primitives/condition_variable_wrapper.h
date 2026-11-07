#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <chrono>

namespace XER::AsyncIO::IO::Sync {

class ConditionVariableWrapper {
public:
    ConditionVariableWrapper(const std::string& name = "");
    ~ConditionVariableWrapper();

    // Signaling
    void Notify();
    void NotifyAll();

    // Waiting
    void Wait(std::unique_lock<std::mutex>& lock);
    bool WaitFor(std::unique_lock<std::mutex>& lock, const std::chrono::milliseconds& timeout);

    // Statistics
    const std::string& GetName() const { return name_; }
    uint64_t GetNotifyCount() const { return notify_count_; }
    uint64_t GetWaitCount() const { return wait_count_; }

private:
    std::condition_variable cv_;
    std::string name_;
    uint64_t notify_count_;
    uint64_t wait_count_;
};

}  // namespace XER::AsyncIO::IO::Sync
