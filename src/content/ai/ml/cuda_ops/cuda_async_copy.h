#pragma once

#include <cstddef>
#include <cuda_runtime.h>
#include <cstdint>
#include <memory>

namespace XER::ML::CudaOps {

/// @brief Async memory transfer between host and device
/// 
/// Provides non-blocking CUDA memory operations with stream management
/// for efficient GPU/CPU data exchange and pipelining.
class CudaAsyncCopy {
public:
    /// Copy data from host to device asynchronously
    /// @param host_src Pointer to host memory (source)
    /// @param device_dst Pointer to device memory (destination)
    /// @param bytes Number of bytes to copy
    /// @param stream CUDA stream for async execution (0 = default)
    /// @return cudaError_t - CUDA error code
    static cudaError_t HostToDevice(const void* host_src,
                                     void* device_dst,
                                     size_t bytes,
                                     cudaStream_t stream = 0);
    
    /// Copy data from device to host asynchronously
    /// @param device_src Pointer to device memory (source)
    /// @param host_dst Pointer to host memory (destination)
    /// @param bytes Number of bytes to copy
    /// @param stream CUDA stream for async execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t DeviceToHost(const void* device_src,
                                     void* host_dst,
                                     size_t bytes,
                                     cudaStream_t stream = 0);
    
    /// Copy data from device to device asynchronously
    /// @param device_src Source device pointer
    /// @param device_dst Destination device pointer
    /// @param bytes Number of bytes to copy
    /// @param stream CUDA stream for async execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t DeviceToDevice(const void* device_src,
                                       void* device_dst,
                                       size_t bytes,
                                       cudaStream_t stream = 0);
    
    /// Allocate pinned (page-locked) host memory for faster transfers
    /// @param bytes Number of bytes to allocate
    /// @return Pointer to pinned memory, nullptr on failure
    /// @note Must be freed with FreePinned()
    static void* AllocatePinned(size_t bytes);
    
    /// Free pinned host memory previously allocated with AllocatePinned()
    /// @param ptr Pointer to pinned memory
    static void FreePinned(void* ptr);
};

} // namespace XER::ML::CudaOps
