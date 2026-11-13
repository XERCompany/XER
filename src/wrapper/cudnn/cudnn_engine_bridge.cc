
#include "wrapper/cudnn/cudnn_engine_bridge.h"

#if ENGINE_HAS_CUDNN_BRIDGE
#include <cudnn.h>
#include <cstdio>
#endif

namespace XER::Bridge::CuDNN {

bool IsAvailable() {
#if ENGINE_HAS_CUDNN_BRIDGE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
#if ENGINE_HAS_CUDNN_BRIDGE
    char buf[64];
    std::snprintf(buf, sizeof(buf), "cuDNN bridge enabled (v%d.%d.%d)",
                  CUDNN_MAJOR, CUDNN_MINOR, CUDNN_PATCHLEVEL);
    return buf;
#else
    return "cuDNN bridge unavailable";
#endif
}

}  // namespace XER::Bridge::CuDNN
