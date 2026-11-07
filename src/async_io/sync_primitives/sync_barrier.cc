#include "sync_barrier.h"

#include <stdexcept>

namespace XER::AsyncIO::IO::Sync {

SyncBarrier::SyncBarrier(int count, const std::string& name)
    : count_(count), waiting_(0), phase_(0), name_(name) {
    if (count <= 0) {
        throw std::invalid_argument("SyncBarrier: count must be > 0");
    }
}

SyncBarrier::~SyncBarrier() = default;

void SyncBarrier::Arrive() {
    std::unique_lock<std::mutex> lock(mutex_);
    ++waiting_;
    if (waiting_ == count_) {
        ++phase_;
        waiting_ = 0;
        cv_.notify_all();
    } else {
        int current_phase = phase_;
        cv_.wait(lock, [this, current_phase] { return phase_ != current_phase; });
    }
}

void SyncBarrier::WaitPhase(int phase) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this, phase] { return phase_ >= phase; });
}

bool SyncBarrier::Reset() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (waiting_ != 0) {
        return false;
    }
    phase_ = 0;
    return true;
}

}  // namespace XER::AsyncIO::IO::Sync
