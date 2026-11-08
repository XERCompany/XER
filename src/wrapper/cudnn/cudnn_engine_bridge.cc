#include "wrapper/cudnn/cudnn_engine_bridge.h"

namespace XER::Bridge::CuDNN {

bool IsAvailable() {
#if ENGINE_HAS_CUDNN_BRIDGE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
    return IsAvailable() ? "cuDNN bridge enabled" : "cuDNN bridge unavailable";
}

}  // namespace XER::Bridge::CuDNN