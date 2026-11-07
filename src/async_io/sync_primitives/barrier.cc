#include "barrier.h"

namespace XER::AsyncIO::IO::Sync {

Barrier::Barrier(int thread_count, const std::string& name)
    : thread_count_(thread_count), waiting_count_(0), generation_(0), name_(name) {}

Barrier::~Barrier() {}

void Barrier::Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    int gen = generation_;
    waiting_count_++;
    
    if (waiting_count_ >= thread_count_) {
        // All threads arrived
        waiting_count_ = 0;
        generation_++;
        cv_.notify_all();
    } else {
        // Wait for other threads
        cv_.wait(lock, [this, gen]() { return generation_ != gen; });
    }
}

bool Barrier::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (waiting_count_ == 0) {
        generation_++;
        return true;
    }
    
    return false;
}

}  // namespace XER::AsyncIO::IO::Sync
