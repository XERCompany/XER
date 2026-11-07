#include "sync_event_listener.h"

namespace XER::AsyncIO::IO::Sync {

SyncEventListener::SyncEventListener(const std::string& event_name)
    : triggered_(false), event_name_(event_name) {}

SyncEventListener::~SyncEventListener() = default;

void SyncEventListener::Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return triggered_; });
}

bool SyncEventListener::WaitFor(const std::chrono::milliseconds& timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return triggered_; });
}

void SyncEventListener::Signal() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        triggered_ = true;
    }
    cv_.notify_all();
}

void SyncEventListener::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    triggered_ = false;
}

bool SyncEventListener::IsTriggered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return triggered_;
}

}  // namespace XER::AsyncIO::IO::Sync
