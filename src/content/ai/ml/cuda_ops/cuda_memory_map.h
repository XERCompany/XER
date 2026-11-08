#pragma once

#include <cuda_runtime.h>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace XER::ML::CudaOps {

/// @brief CUDA device memory management and mapping utilities
/// 
/// Provides wrappers for device memory allocation, deallocation, and
/// safe access patterns with automatic error handling and memory tracking.
class CudaMemoryMap {
public:
    /// Allocate device memory
    /// @param bytes Number of bytes to allocate
    /// @return Pointer to device memory, nullptr on failure
    /// @note Must be freed with Free()
    static void* Allocate(size_t bytes);
    
    /// Free device memory previously allocated with Allocate()
    /// @param ptr Pointer to device memory
    /// @return cudaError_t - CUDA error code
    static cudaError_t Free(void* ptr);
    
    /// Get size of allocated device memory block
    /// @param ptr Pointer to device memory
    /// @return Size in bytes, 0 if ptr is invalid
    static size_t GetSize(void* ptr);
    
    /// Get total device memory usage
    /// @return Total allocated memory in bytes
    static size_t GetTotalAllocated();
    
    /// Get available device memory
    /// @param free_bytes Output: free memory in bytes
    /// @param total_bytes Output: total device memory in bytes
    /// @return cudaError_t - CUDA error code
    static cudaError_t GetMemoryInfo(size_t* free_bytes, size_t* total_bytes);
    
    /// Reset all device memory (destructive)
    /// @return cudaError_t - CUDA error code
    static cudaError_t Reset();
};

/// @brief RAII wrapper for device memory
/// 
/// Automatically allocates memory on construction and frees on destruction.
/// Provides safe exception-proof memory management.
class CudaMemoryHandle {
public:
    /// Allocate device memory
    /// @param bytes Number of bytes to allocate
    /// @throws std::bad_alloc if allocation fails
    explicit CudaMemoryHandle(size_t bytes);
    
    /// Destructor - automatically frees memory
    ~CudaMemoryHandle();
    
    /// Get raw device pointer
    void* Get() const { return ptr_; }
    
    /// Get memory size in bytes
    size_t GetSize() const { return size_; }
    
    /// Release ownership (caller responsible for freeing)
    void* Release();
    
private:
    void* ptr_;
    size_t size_;
};

} // namespace XER::ML::CudaOps
