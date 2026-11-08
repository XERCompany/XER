#pragma once

#include <cuda_runtime.h>
#include <cstddef>
#include <cstdint>

namespace XER::ML::CudaOps {

/// @brief CUDA stream management and synchronization utilities
/// 
/// Provides wrappers for CUDA stream creation, management, and
/// synchronization for pipelined kernel execution and concurrent operations.
class CudaStreamOps {
public:
    /// Create a CUDA stream
    /// @param stream Output parameter for the created stream
    /// @param blocking Whether stream is blocking mode
    /// @return cudaError_t - CUDA error code
    static cudaError_t CreateStream(cudaStream_t* stream, bool blocking = false);
    
    /// Destroy a CUDA stream
    /// @param stream Stream to destroy
    /// @return cudaError_t - CUDA error code
    static cudaError_t DestroyStream(cudaStream_t stream);
    
    /// Synchronize stream (wait for all operations to complete)
    /// @param stream Stream to synchronize
    /// @return cudaError_t - CUDA error code
    static cudaError_t Synchronize(cudaStream_t stream);
    
    /// Record event on stream (for timing and dependencies)
    /// @param event Output parameter for the recorded event
    /// @param stream Stream to record on
    /// @return cudaError_t - CUDA error code
    static cudaError_t RecordEvent(cudaEvent_t* event, cudaStream_t stream);
    
    /// Wait for event completion (cross-stream dependency)
    /// @param stream Stream that waits
    /// @param event Event to wait for
    /// @return cudaError_t - CUDA error code
    static cudaError_t WaitEvent(cudaStream_t stream, cudaEvent_t event);
    
    /// Query event completion status (non-blocking)
    /// @param event Event to query
    /// @param is_completed Output: true if event has occurred
    /// @return cudaError_t - CUDA error code
    static cudaError_t QueryEvent(cudaEvent_t event, bool* is_completed);
    
    /// Measure elapsed time between two events in milliseconds
    /// @param event_start Start event
    /// @param event_end End event
    /// @param milliseconds Output: elapsed time in ms
    /// @return cudaError_t - CUDA error code
    static cudaError_t ElapsedTime(cudaEvent_t event_start,
                                    cudaEvent_t event_end,
                                    float* milliseconds);
};

} // namespace XER::ML::CudaOps
