#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#if __has_include(<pthreadpool.h>)
#include <pthreadpool.h>
#define XER_AI_HAS_PTHREADPOOL 1
#else
#define XER_AI_HAS_PTHREADPOOL 0
#endif

namespace XER::ML::Utils {

/// @brief Thread pool wrapper around pthreadpool
/// 
/// Work-stealing thread pool for efficient parallel data loading,
/// preprocessing, and augmentation during training.
/// 
/// Features:
/// - Automatic core detection
/// - Work-stealing queue for load balancing  
/// - Non-blocking task submission
/// - Bulk synchronization support
class ThreadPool {
public:
    /// Create thread pool with specified number of threads
    /// @param num_threads Number of worker threads (0 = auto-detect)
    /// @throws std::runtime_error if pool creation fails
    explicit ThreadPool(size_t num_threads = 0);
    
    /// Destructor - waits for pending tasks
    ~ThreadPool();
    
    /// Get number of threads in pool
    size_t GetNumThreads() const;
    
    /// Run task in parallel across num_iterations
    /// @param task Function to run: void(void*, size_t index)
    /// @param context User data passed to task
    /// @param num_iterations Number of iterations to parallelize
    /// @note Blocks until all tasks complete
    void ParallelFor(std::function<void(size_t)> task, size_t num_iterations);
    
    /// 1D tile parallelization
    /// @param task Function to run: void(void*, size_t block_start, size_t block_size)
    /// @param context User data
    /// @param total_size Total iteration count
    /// @param tile_size Size of each work tile
    void ParallelForTile1D(std::function<void(size_t, size_t)> task,
                           size_t total_size,
                           size_t tile_size);
    
    /// 2D tile parallelization (for image processing)
    /// @param task Function: void(void*, size_t h_start, size_t h_size, 
    ///                                 size_t w_start, size_t w_size)
    /// @param context User data
    /// @param height Total height
    /// @param width Total width
    /// @param tile_h Tile height
    /// @param tile_w Tile width
    void ParallelFor2DTile(std::function<void(size_t, size_t, size_t, size_t)> task,
                           size_t height, size_t width,
                           size_t tile_h, size_t tile_w);

private:
#if XER_AI_HAS_PTHREADPOOL
    void* threadpool_;
#else
    size_t num_threads_;
#endif
};

} // namespace XER::ML::Utils
