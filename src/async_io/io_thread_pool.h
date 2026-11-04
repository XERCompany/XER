#ifndef IO_ASYNC_IO_THREAD_POOL_H
#define IO_ASYNC_IO_THREAD_POOL_H

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace AsyncIO::IO::Async {

class IOThreadPool {
public:
    explicit IOThreadPool(size_t threadCount = std::thread::hardware_concurrency());
    ~IOThreadPool();

    static IOThreadPool& GetSharedInstance();

    IOThreadPool(const IOThreadPool &) = delete;
    IOThreadPool &operator=(const IOThreadPool &) = delete;

    void Start();
    void Stop();
    void Enqueue(std::function<void()> task);
    // Non-blocking enqueue with backpressure: returns false if the pool is
    // stopped or the pending queue has reached max_pending. max_pending==0
    // means unlimited (legacy behavior).
    bool TryEnqueue(std::function<void()> task, size_t max_pending = 0);
    // Block until all currently-enqueued tasks finish. Tasks enqueued
    // concurrently with WaitAll are not guaranteed to be awaited.
    void WaitAll();
    size_t GetThreadCount() const;
    size_t GetPendingTaskCount() const;
    bool IsRunning() const;

private:
    void WorkerLoop();

    size_t configured_thread_count_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;   // signaled when a task finishes
    uint64_t active_tasks_ = 0;          // tasks currently executing
    bool stopping_ = false;
    bool started_ = false;
};

} // namespace AsyncIO::IO::Async

#endif // IO_ASYNC_IO_THREAD_POOL_H
