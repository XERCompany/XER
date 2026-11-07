#include "io_priority_queue.h"

namespace XER::AsyncIO::IO::Async {

void IOPriorityQueue::Push(IOPriorityItem item) {
    {
        std::lock_guard lock(mutex_);
        queue_.push(std::move(item));
    }
    cv_.notify_one();
}

bool IOPriorityQueue::TryPop(IOPriorityItem &item) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    item = queue_.top();
    queue_.pop();
    return true;
}

void IOPriorityQueue::WaitPop(IOPriorityItem &item) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty(); });
    item = queue_.top();
    queue_.pop();
}

bool IOPriorityQueue::Empty() const {
    std::lock_guard lock(mutex_);
    return queue_.empty();
}

} // namespace XER::AsyncIO::IO::Async
