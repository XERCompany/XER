#include "cuda_stream_ops.h"

#include <cuda_runtime.h>

namespace XER::ML::CudaOps {

cudaError_t CudaStreamOps::CreateStream(cudaStream_t* stream, bool blocking) {
    if (!stream) {
        return cudaErrorInvalidValue;
    }
    
    unsigned int flags = blocking ? cudaStreamDefault : cudaStreamNonBlocking;
    return cudaStreamCreateWithFlags(stream, flags);
}

cudaError_t CudaStreamOps::DestroyStream(cudaStream_t stream) {
    return cudaStreamDestroy(stream);
}

cudaError_t CudaStreamOps::Synchronize(cudaStream_t stream) {
    return cudaStreamSynchronize(stream);
}

cudaError_t CudaStreamOps::RecordEvent(cudaEvent_t* event, cudaStream_t stream) {
    if (!event) {
        return cudaErrorInvalidValue;
    }
    
    cudaError_t err = cudaEventCreate(event);
    if (err != cudaSuccess) {
        return err;
    }
    
    return cudaEventRecord(*event, stream);
}

cudaError_t CudaStreamOps::WaitEvent(cudaStream_t stream, cudaEvent_t event) {
    return cudaStreamWaitEvent(stream, event);
}

cudaError_t CudaStreamOps::QueryEvent(cudaEvent_t event, bool* is_completed) {
    if (!is_completed) {
        return cudaErrorInvalidValue;
    }
    
    cudaError_t err = cudaEventQuery(event);
    
    if (err == cudaSuccess) {
        *is_completed = true;
    } else if (err == cudaErrorNotReady) {
        *is_completed = false;
        return cudaSuccess;
    }
    
    return err;
}

cudaError_t CudaStreamOps::ElapsedTime(cudaEvent_t event_start,
                                        cudaEvent_t event_end,
                                        float* milliseconds) {
    if (!milliseconds) {
        return cudaErrorInvalidValue;
    }
    
    return cudaEventElapsedTime(milliseconds, event_start, event_end);
}

} // namespace XER::ML::CudaOps
