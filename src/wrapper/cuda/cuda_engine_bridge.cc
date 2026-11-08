#include "wrapper/cuda/cuda_engine_bridge.h"

namespace XER::Bridge::Cuda {

bool IsAvailable() {
#if ENGINE_HAS_CUDA_BRIDGE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
    return IsAvailable() ? "CUDA bridge enabled" : "CUDA bridge unavailable";
}

}  // namespace XER::Bridge::Cuda