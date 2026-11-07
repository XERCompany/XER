#pragma once

#include <queue>
#include <memory>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <set>
#include <cstdint>

namespace XER::AsyncIO::IO::Async {

// Task functor type
using AsyncTask = std::function<void()>;
using CompletionCallback = std::function<void(bool)>;

struct QueuedTask {
    AsyncTask task;
    CompletionCallback on_complete;
    int priority;
    uint64_t task_id;
    
    QueuedTask(AsyncTask t, CompletionCallback cb, int p, uint64_t id)
        : task(t), on_complete(cb), priority(p), task_id(id) {}
    
    bool operator<(const QueuedTask& other) const {
        return priority < other.priority;
    }
};

class AsyncTaskQueue {
public:
    AsyncTaskQueue(size_t max_queue_size = 1000);
    ~AsyncTaskQueue();

    // Task enqueueing
    uint64_t EnqueueTask(AsyncTask task, int priority = 0, CompletionCallback on_complete = nullptr);
    bool TryEnqueueTask(AsyncTask task, int priority = 0, CompletionCallback on_complete = nullptr);

    // Task dequeuing
    bool DequeueTask(QueuedTask& task);
    // Blocking dequeue: waits up to timeout_ms for a task to become available.
    // Returns false on timeout or shutdown. timeout_ms < 0 means wait forever.
    bool WaitAndDequeue(QueuedTask& task, int timeout_ms);
    int GetQueueSize() const;
    bool IsEmpty() const;

    // Queue control
    void Clear();
    void Shutdown();
    bool IsRunning() const;

    // Cancellation
    bool CancelTask(uint64_t task_id);
    size_t GetCancelledCount() const;
    // Reclaim cancelled task IDs that never get dequeued (e.g. cancelled
    // before they were ever scheduled). Periodic cleanup prevents the
    // cancelled set from growing without bound.
    void PruneCancelled();

    // Statistics
    uint64_t GetTotalTasksProcessed() const;
    size_t GetMaxQueueSize() const;

private:
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::priority_queue<QueuedTask> task_queue_;
    std::set<uint64_t> cancelled_tasks_;
    size_t max_queue_size_;
    uint64_t task_id_counter_;
    uint64_t tasks_processed_;
    bool is_running_;
};

}  // namespace XER::AsyncIO::IO::Async
