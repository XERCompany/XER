
#include "wrapper/cuda/cuda_engine_bridge.h"

#if ENGINE_HAS_CUDA_BRIDGE
#include <cuda_runtime.h>
#include <cstdio>
#endif

namespace XER::Bridge::Cuda {

bool IsAvailable() {
#if ENGINE_HAS_CUDA_BRIDGE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
#if ENGINE_HAS_CUDA_BRIDGE
    char buf[64];
    std::snprintf(buf, sizeof(buf), "CUDA bridge enabled (runtime %d.%d)",
                  CUDART_VERSION / 1000, (CUDART_VERSION % 1000) / 10);
    return buf;
#else
    return "CUDA bridge unavailable";
#endif
}

}  // namespace XER::Bridge::Cuda
