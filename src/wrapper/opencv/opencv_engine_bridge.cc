#include "wrapper/opencv/opencv_engine_bridge.h"

namespace XER::engine::bridge::opencv {

bool IsAvailable() {
#if ENGINE_HAS_OPENCV_BRIDGE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
    return IsAvailable() ? "OpenCV bridge enabled" : "OpenCV bridge unavailable";
}

}  // namespace XER::engine::bridge::opencv