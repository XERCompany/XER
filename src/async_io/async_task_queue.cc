#include "async_task_queue.h"

#include <chrono>

namespace XER::AsyncIO::IO::Async {

AsyncTaskQueue::AsyncTaskQueue(size_t max_queue_size)
    : max_queue_size_(max_queue_size), task_id_counter_(0), tasks_processed_(0), is_running_(true) {}

AsyncTaskQueue::~AsyncTaskQueue() {
    Shutdown();
}

uint64_t AsyncTaskQueue::EnqueueTask(AsyncTask task, int priority, CompletionCallback on_complete) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this] {
        return !is_running_ || task_queue_.size() < max_queue_size_;
    });
    if (!is_running_) {
        return 0;  // 0 = "not enqueued / shutdown"
    }

    // task_id starts at 1 so 0 is reserved as the "no task" sentinel.
    uint64_t task_id = ++task_id_counter_;
    if (task_id == 0) task_id = ++task_id_counter_;  // wrap-around safety
    QueuedTask q_task(task, on_complete, priority, task_id);
    task_queue_.push(q_task);

    queue_cv_.notify_one();
    return task_id;
}

bool AsyncTaskQueue::TryEnqueueTask(AsyncTask task, int priority, CompletionCallback on_complete) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (!is_running_ || task_queue_.size() >= max_queue_size_) {
        return false;
    }

    uint64_t task_id = ++task_id_counter_;
    if (task_id == 0) task_id = ++task_id_counter_;
    QueuedTask q_task(task, on_complete, priority, task_id);
    task_queue_.push(q_task);

    queue_cv_.notify_one();
    return true;
}

bool AsyncTaskQueue::DequeueTask(QueuedTask& task) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    while (is_running_) {
        if (task_queue_.empty()) {
            return false;  // non-blocking: caller may poll or use WaitAndDequeue
        }

        task = task_queue_.top();
        task_queue_.pop();
        queue_cv_.notify_one();

        if (cancelled_tasks_.count(task.task_id)) {
            cancelled_tasks_.erase(task.task_id);
            continue;  // skip cancelled, try next
        }

        ++tasks_processed_;
        return true;
    }

    return false;
}

bool AsyncTaskQueue::WaitAndDequeue(QueuedTask& task, int timeout_ms) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    auto pred = [this] { return !is_running_ || !task_queue_.empty(); };

    if (timeout_ms < 0) {
        queue_cv_.wait(lock, pred);
    } else {
        if (!queue_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred)) {
            return false;  // timed out
        }
    }
    if (!is_running_ && task_queue_.empty()) {
        return false;
    }

    while (!task_queue_.empty()) {
        task = task_queue_.top();
        task_queue_.pop();
        queue_cv_.notify_one();
        if (cancelled_tasks_.count(task.task_id)) {
            cancelled_tasks_.erase(task.task_id);
            continue;
        }
        ++tasks_processed_;
        return true;
    }
    return false;
}

int AsyncTaskQueue::GetQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return static_cast<int>(task_queue_.size());
}

bool AsyncTaskQueue::IsEmpty() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.empty();
}

void AsyncTaskQueue::Clear() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!task_queue_.empty()) {
        task_queue_.pop();
    }
    cancelled_tasks_.clear();
    queue_cv_.notify_all();
}

void AsyncTaskQueue::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        is_running_ = false;
        while (!task_queue_.empty()) {
            task_queue_.pop();
        }
        cancelled_tasks_.clear();
    }
    queue_cv_.notify_all();
}

bool AsyncTaskQueue::IsRunning() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return is_running_;
}

bool AsyncTaskQueue::CancelTask(uint64_t task_id) {
    if (task_id == 0) return false;  // 0 is the "no task" sentinel, never valid
    std::lock_guard<std::mutex> lock(queue_mutex_);
    cancelled_tasks_.insert(task_id);
    return true;
}

size_t AsyncTaskQueue::GetCancelledCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return cancelled_tasks_.size();
}

void AsyncTaskQueue::PruneCancelled() {
    // The cancelled set only ever shrinks via DequeueTask/WaitAndDequeue
    // (when a cancelled task is popped) or Clear/Shutdown. If a task is
    // cancelled but never reaches the front of the queue (e.g. it was
    // cancelled while still in the queue and other higher-priority tasks
    // keep getting enqueued), its ID lingers. This prunes IDs that are no
    // longer present in the queue.
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (cancelled_tasks_.empty()) return;

    // Rebuild the set of IDs still in the queue. priority_queue doesn't
    // expose iteration, so we drain and re-push (O(n log n) but rare).
    std::set<uint64_t> in_queue;
    std::priority_queue<QueuedTask> backup;
    while (!task_queue_.empty()) {
        in_queue.insert(task_queue_.top().task_id);
        backup.push(task_queue_.top());
        task_queue_.pop();
    }
    task_queue_ = std::move(backup);

    std::set<uint64_t> pruned;
    for (uint64_t id : cancelled_tasks_) {
        if (in_queue.count(id)) {
            pruned.insert(id);
        }
    }
    cancelled_tasks_ = std::move(pruned);
}

uint64_t AsyncTaskQueue::GetTotalTasksProcessed() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_processed_;
}

size_t AsyncTaskQueue::GetMaxQueueSize() const {
    return max_queue_size_;
}

}  // namespace XER::AsyncIO::IO::Async
