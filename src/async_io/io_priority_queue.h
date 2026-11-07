#ifndef IO_ASYNC_IO_PRIORITY_QUEUE_H
#define IO_ASYNC_IO_PRIORITY_QUEUE_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace XER::AsyncIO::IO::Async {

struct IOPriorityItem {
    int priority;
    std::function<void()> task;
};

struct IOPriorityCompare {
    bool operator()(IOPriorityItem const &a, IOPriorityItem const &b) const {
        return a.priority < b.priority;
    }
};

class IOPriorityQueue {
public:
    void Push(IOPriorityItem item);
    bool TryPop(IOPriorityItem &item);
    void WaitPop(IOPriorityItem &item);
    bool Empty() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<IOPriorityItem, std::vector<IOPriorityItem>, IOPriorityCompare> queue_;
};

} // namespace XER::AsyncIO::IO::Async

#endif // IO_ASYNC_IO_PRIORITY_QUEUE_H
