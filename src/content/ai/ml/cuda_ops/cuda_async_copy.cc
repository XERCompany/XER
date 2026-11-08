#include "cuda_async_copy.h"

#include <cuda_runtime.h>
#include <stdexcept>

namespace XER::ML::CudaOps {

cudaError_t CudaAsyncCopy::HostToDevice(const void* host_src,
                                         void* device_dst,
                                         size_t bytes,
                                         cudaStream_t stream) {
    if (!host_src || !device_dst || bytes == 0) {
        return cudaErrorInvalidValue;
    }
    
    return cudaMemcpyAsync(device_dst, host_src, bytes,
                          cudaMemcpyHostToDevice, stream);
}

cudaError_t CudaAsyncCopy::DeviceToHost(const void* device_src,
                                         void* host_dst,
                                         size_t bytes,
                                         cudaStream_t stream) {
    if (!device_src || !host_dst || bytes == 0) {
        return cudaErrorInvalidValue;
    }
    
    return cudaMemcpyAsync(host_dst, device_src, bytes,
                          cudaMemcpyDeviceToHost, stream);
}

cudaError_t CudaAsyncCopy::DeviceToDevice(const void* device_src,
                                           void* device_dst,
                                           size_t bytes,
                                           cudaStream_t stream) {
    if (!device_src || !device_dst || bytes == 0) {
        return cudaErrorInvalidValue;
    }
    
    return cudaMemcpyAsync(device_dst, device_src, bytes,
                          cudaMemcpyDeviceToDevice, stream);
}

void* CudaAsyncCopy::AllocatePinned(size_t bytes) {
    if (bytes == 0) {
        return nullptr;
    }
    
    void* ptr = nullptr;
    cudaError_t err = cudaHostAlloc(&ptr, bytes, cudaHostAllocDefault);
    
    if (err != cudaSuccess) {
        return nullptr;
    }
    
    return ptr;
}

void CudaAsyncCopy::FreePinned(void* ptr) {
    if (ptr != nullptr) {
        cudaFreeHost(ptr);
    }
}

} // namespace XER::ML::CudaOps
