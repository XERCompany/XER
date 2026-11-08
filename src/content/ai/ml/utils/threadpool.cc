#include "threadpool.h"

#include <stdexcept>
#include <algorithm>
#include <thread>

namespace XER::ML::Utils {

ThreadPool::ThreadPool(size_t num_threads) {
    // Auto-detect if num_threads = 0
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) {
            num_threads = 4;  // Fallback
        }
    }
    
#if XER_AI_HAS_PTHREADPOOL
    threadpool_ = static_cast<void*>(pthreadpool_create(num_threads));
    if (threadpool_ == nullptr) {
        throw std::runtime_error("Failed to create pthreadpool");
    }
#else
    num_threads_ = num_threads;
#endif
}

ThreadPool::~ThreadPool() {
#if XER_AI_HAS_PTHREADPOOL
    if (threadpool_ != nullptr) {
        pthreadpool_destroy(static_cast<pthreadpool_t>(threadpool_));
    }
#endif
}

size_t ThreadPool::GetNumThreads() const {
#if XER_AI_HAS_PTHREADPOOL
    return pthreadpool_get_threads_count(static_cast<pthreadpool_t>(threadpool_));
#else
    return num_threads_;
#endif
}

void ThreadPool::ParallelFor(std::function<void(size_t)> task, size_t num_iterations) {
    if (num_iterations == 0) {
        return;
    }

#if XER_AI_HAS_PTHREADPOOL
    // Wrapper to call std::function from C-style callback
    struct Context {
        std::function<void(size_t)> task;
    } context{task};
    
    auto wrapper = [](void* context, size_t index) {
        auto ctx = static_cast<Context*>(context);
        ctx->task(index);
    };

    pthreadpool_parallelize_1d(
        static_cast<pthreadpool_t>(threadpool_),
        wrapper,
        &context,
        num_iterations,
        0U);
#else
    const size_t workers = std::max<size_t>(1, num_threads_);
    const size_t chunk = (num_iterations + workers - 1U) / workers;
    std::vector<std::thread> pool;
    pool.reserve(workers);

    for (size_t worker = 0; worker < workers; ++worker) {
        const size_t begin = worker * chunk;
        if (begin >= num_iterations) {
            break;
        }
        const size_t end = std::min(num_iterations, begin + chunk);
        pool.emplace_back([begin, end, &task]() {
            for (size_t index = begin; index < end; ++index) {
                task(index);
            }
        });
    }

    for (auto& worker : pool) {
        if (worker.joinable()) {
            worker.join();
        }
    }
#endif
}

void ThreadPool::ParallelForTile1D(std::function<void(size_t, size_t)> task,
                                   size_t total_size,
                                   size_t tile_size) {
    if (total_size == 0 || tile_size == 0) {
        return;
    }

#if XER_AI_HAS_PTHREADPOOL
    struct Context {
        std::function<void(size_t, size_t)> task;
    } context{task};
    
    auto wrapper = [](void* context, size_t block_start, size_t block_size) {
        auto ctx = static_cast<Context*>(context);
        ctx->task(block_start, block_size);
    };
    
    pthreadpool_parallelize_1d_tile_1d(
        static_cast<pthreadpool_t>(threadpool_),
        wrapper,
        &context,
        total_size,
        tile_size,
        0U);
#else
    const size_t tiles = (total_size + tile_size - 1U) / tile_size;
    ParallelFor([&task, total_size, tile_size](size_t tile_index) {
        const size_t block_start = tile_index * tile_size;
        const size_t block_size = std::min(tile_size, total_size - block_start);
        task(block_start, block_size);
    }, tiles);
#endif
}

void ThreadPool::ParallelFor2DTile(std::function<void(size_t, size_t, size_t, size_t)> task,
                                   size_t height, size_t width,
                                   size_t tile_h, size_t tile_w) {
    if (height == 0 || width == 0 || tile_h == 0 || tile_w == 0) {
        return;
    }

#if XER_AI_HAS_PTHREADPOOL
    struct Context {
        std::function<void(size_t, size_t, size_t, size_t)> task;
    } context{task};
    
    auto wrapper = [](void* context, size_t h_start, size_t h_size,
                      size_t w_start, size_t w_size) {
        auto ctx = static_cast<Context*>(context);
        ctx->task(h_start, h_size, w_start, w_size);
    };
    
    pthreadpool_parallelize_2d_tile_2d(
        static_cast<pthreadpool_t>(threadpool_),
        wrapper,
        &context,
        height,
        width,
        tile_h,
        tile_w,
        0U);
#else
    const size_t h_tiles = (height + tile_h - 1U) / tile_h;
    const size_t w_tiles = (width + tile_w - 1U) / tile_w;
    const size_t total_tiles = h_tiles * w_tiles;
    ParallelFor([&task, height, width, tile_h, tile_w, w_tiles](size_t tile_index) {
        const size_t h_tile = tile_index / w_tiles;
        const size_t w_tile = tile_index % w_tiles;

        const size_t h_start = h_tile * tile_h;
        const size_t w_start = w_tile * tile_w;
        const size_t h_size = std::min(tile_h, height - h_start);
        const size_t w_size = std::min(tile_w, width - w_start);
        if (h_size > 0U && w_size > 0U) {
            task(h_start, h_size, w_start, w_size);
        }
    }, total_tiles);
#endif
}

} // namespace XER::ML::Utils
