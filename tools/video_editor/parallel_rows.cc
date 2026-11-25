// parallel_rows.cc — see parallel_rows.h for the contract.
//
// The pool is a lazily-created process-wide singleton: the compositor
// (playback worker) and the AutoCacher's private compositor both feed
// it, so submissions are serialized at whole-pass granularity — two
// renderers never interleave on the workers, each still gets full
// intra-pass parallelism.
//
// Dispatch: the caller publishes (fn, total units, chunk size), bumps
// a generation counter, wakes the workers, then drains chunks off the
// same atomic cursor itself. Workers grab [b, b+chunk) ranges until
// the cursor passes total; the pass ends when the completed-unit
// counter reaches total — which can only happen after every fn call
// returned, so no worker outlives the caller's stack.
#include "parallel_rows.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace XER::VideoEditor {
namespace {

class RenderPool {
public:
    static RenderPool& Instance() {
        static RenderPool pool;
        return pool;
    }

    int MaxThreads() const { return workers_total_; }

    // Serialized per-pass: concurrent callers (preview compositor,
    // AutoCacher compositor) take turns on the whole pool.
    void Run(int units, int parts,
             const std::function<void(int b, int e)>& fn) {
        std::lock_guard<std::mutex> submit_lock(submit_mutex_);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            job_fn_ = &fn;
            job_total_ = units;
            // ~4 chunks per part: enough for load balance without
            // fragmenting per-thread cache locality.
            job_chunk_ = std::max(
                1, units / std::max(1, parts * 4));
            job_next_.store(0, std::memory_order_relaxed);
            job_done_.store(0, std::memory_order_relaxed);
            ++generation_;
        }
        cv_work_.notify_all();
        DrainChunks();
        std::unique_lock<std::mutex> lk(mutex_);
        cv_done_.wait(lk, [&] {
            return job_done_.load(std::memory_order_acquire) >=
                   job_total_;
        });
        job_fn_ = nullptr;
    }

private:
    RenderPool() {
        const unsigned hw = std::thread::hardware_concurrency();
        workers_total_ = static_cast<int>(
            std::max(2u, std::min(hw, 16u)));
        workers_.reserve(
            static_cast<std::size_t>(workers_total_ - 1));
        for (int i = 0; i < workers_total_ - 1; ++i) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }
    }

    ~RenderPool() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stop_ = true;
            ++generation_;
        }
        cv_work_.notify_all();
        for (auto& t : workers_) {
            t.join();
        }
    }

    // Shared chunk-drain: identical for the submitting caller and
    // every worker — pull a [b, e) range, run it, account units.
    void DrainChunks() {
        for (;;) {
            const int b =
                job_next_.fetch_add(job_chunk_,
                                    std::memory_order_relaxed);
            if (b >= job_total_) {
                return;
            }
            const int e = std::min(b + job_chunk_, job_total_);
            (*job_fn_)(b, e);
            const int prev =
                job_done_.fetch_add(e - b, std::memory_order_acq_rel);
            if (prev + (e - b) >= job_total_) {
                cv_done_.notify_all();
                return;
            }
        }
    }

    void WorkerLoop() {
        std::uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(mutex_);
                cv_work_.wait(lk, [&] {
                    return stop_ || generation_ != seen;
                });
                if (stop_) {
                    return;
                }
                seen = generation_;
            }
            DrainChunks();
        }
    }

    // workers_total_ = pool threads INCLUDING the submitting caller.
    int workers_total_ = 1;
    std::vector<std::thread> workers_;

    std::mutex submit_mutex_;   // one parallel pass at a time
    std::mutex mutex_;          // guards generation_/stop_/cv waits
    std::condition_variable cv_work_;
    std::condition_variable cv_done_;
    std::uint64_t generation_ = 0;
    bool stop_ = false;

    // Job state — valid only while a Run is in flight (the caller
    // blocks on job_done_ == job_total_ before clearing job_fn_).
    const std::function<void(int b, int e)>* job_fn_ = nullptr;
    std::atomic<int> job_next_{0};
    std::atomic<int> job_done_{0};
    int job_total_ = 0;
    int job_chunk_ = 1;
};

}  // namespace

int VeRenderThreadCount() {
    const int pool_max = RenderPool::Instance().MaxThreads();
    const char* env = std::getenv("XER_VE_THREADS");
    if (env == nullptr || *env == '\0') {
        return std::min(8, pool_max);
    }
    char* end = nullptr;
    const long n = std::strtol(env, &end, 10);
    if (end == env || *end != '\0') {
        return std::min(8, pool_max);   // unparseable -> default
    }
    if (n <= 1) {
        return 1;                        // serial
    }
    return std::min(static_cast<int>(n), pool_max);
}

void VeParallelFor(int units,
                   const std::function<void(int b, int e)>& fn) {
    if (units <= 0 || fn == nullptr) {
        return;
    }
    const int threads = VeRenderThreadCount();
    if (threads <= 1 || units < kVeParallelMinUnits) {
        fn(0, units);
        return;
    }
    RenderPool::Instance().Run(units, std::min(threads, units), fn);
}

}  // namespace XER::VideoEditor
