#include "cuda_memory_map.h"

#include <cuda_runtime.h>
#include <unordered_map>
#include <mutex>
#include <stdexcept>

namespace XER::ML::CudaOps {

// Global memory tracker (for GetTotalAllocated)
static std::unordered_map<uintptr_t, size_t> g_memory_map;
static std::mutex g_memory_mutex;

void* CudaMemoryMap::Allocate(size_t bytes) {
    if (bytes == 0) {
        return nullptr;
    }
    
    void* ptr = nullptr;
    cudaError_t err = cudaMalloc(&ptr, bytes);
    
    if (err != cudaSuccess) {
        return nullptr;
    }
    
    // Track allocation
    {
        std::lock_guard<std::mutex> lock(g_memory_mutex);
        g_memory_map[reinterpret_cast<uintptr_t>(ptr)] = bytes;
    }
    
    return ptr;
}

cudaError_t CudaMemoryMap::Free(void* ptr) {
    if (ptr == nullptr) {
        return cudaSuccess;
    }
    
    // Remove from tracking
    {
        std::lock_guard<std::mutex> lock(g_memory_mutex);
        g_memory_map.erase(reinterpret_cast<uintptr_t>(ptr));
    }
    
    return cudaFree(ptr);
}

size_t CudaMemoryMap::GetSize(void* ptr) {
    if (ptr == nullptr) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(g_memory_mutex);
    auto it = g_memory_map.find(reinterpret_cast<uintptr_t>(ptr));
    if (it != g_memory_map.end()) {
        return it->second;
    }
    
    return 0;
}

size_t CudaMemoryMap::GetTotalAllocated() {
    std::lock_guard<std::mutex> lock(g_memory_mutex);
    size_t total = 0;
    for (const auto& pair : g_memory_map) {
        total += pair.second;
    }
    return total;
}

cudaError_t CudaMemoryMap::GetMemoryInfo(size_t* free_bytes, size_t* total_bytes) {
    if (!free_bytes || !total_bytes) {
        return cudaErrorInvalidValue;
    }
    
    return cudaMemGetInfo(free_bytes, total_bytes);
}

cudaError_t CudaMemoryMap::Reset() {
    {
        std::lock_guard<std::mutex> lock(g_memory_mutex);
        g_memory_map.clear();
    }
    
    return cudaDeviceReset();
}

// CudaMemoryHandle implementation

CudaMemoryHandle::CudaMemoryHandle(size_t bytes)
    : ptr_(CudaMemoryMap::Allocate(bytes)), size_(bytes) {
    if (ptr_ == nullptr && bytes > 0) {
        throw std::bad_alloc();
    }
}

CudaMemoryHandle::~CudaMemoryHandle() {
    if (ptr_ != nullptr) {
        CudaMemoryMap::Free(ptr_);
    }
}

void* CudaMemoryHandle::Release() {
    void* tmp = ptr_;
    ptr_ = nullptr;
    size_ = 0;
    return tmp;
}

} // namespace XER::ML::CudaOps
