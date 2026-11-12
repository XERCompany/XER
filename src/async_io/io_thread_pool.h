#ifndef IO_ASYNC_IO_THREAD_POOL_H
#define IO_ASYNC_IO_THREAD_POOL_H

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace XER::AsyncIO::IO::Async {

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
    // Move-only task overload: the queue stores std::function, which requires
    // copyable targets, so the callable is heap-held via shared_ptr and
    // invoked through a copyable trampoline. Use this for lambdas capturing
    // std::promise/std::future/std::unique_ptr.
    template <typename F>
    void Enqueue(F&& task) {
        auto shared = std::make_shared<std::decay_t<F>>(std::forward<F>(task));
        Enqueue(std::function<void()>(
            [shared]() mutable { (*shared)(); }));
    }
    // Non-blocking enqueue with backpressure: returns false if the pool is
    // stopped or the pending queue has reached max_pending. max_pending==0
    // means unlimited (legacy behavior).
    bool TryEnqueue(std::function<void()> task, size_t max_pending = 0);
    template <typename F>
    bool TryEnqueue(F&& task, size_t max_pending = 0) {
        auto shared = std::make_shared<std::decay_t<F>>(std::forward<F>(task));
        return TryEnqueue(std::function<void()>(
            [shared]() mutable { (*shared)(); }), max_pending);
    }
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

} // namespace XER::AsyncIO::IO::Async

#endif // IO_ASYNC_IO_THREAD_POOL_H
