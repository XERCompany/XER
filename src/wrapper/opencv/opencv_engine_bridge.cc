#include "wrapper/opencv/opencv_engine_bridge.h"

namespace XER::Bridge::OpenCV {

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

}  // namespace XER::Bridge::OpenCV