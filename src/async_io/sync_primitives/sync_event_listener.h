#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <chrono>

namespace XER::AsyncIO::IO::Sync {

class SyncEventListener {
public:
    explicit SyncEventListener(const std::string& event_name = "");
    ~SyncEventListener();

    // Waiting
    void Wait();
    bool WaitFor(const std::chrono::milliseconds& timeout);

    // Signaling
    void Signal();
    void Reset();

    // State
    bool IsTriggered() const;
    const std::string& GetEventName() const { return event_name_; }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool triggered_;
    std::string event_name_;
};

}  // namespace XER::AsyncIO::IO::Sync
