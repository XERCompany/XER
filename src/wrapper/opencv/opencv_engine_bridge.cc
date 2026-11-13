
#include "wrapper/opencv/opencv_engine_bridge.h"

#if ENGINE_HAS_OPENCV_BRIDGE
#include <opencv2/core/version.hpp>
#include <cstdio>
#endif

namespace XER::Bridge::OpenCV {

bool IsAvailable() {
#if ENGINE_HAS_OPENCV_BRIDGE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
#if ENGINE_HAS_OPENCV_BRIDGE
    char buf[96];
    std::snprintf(buf, sizeof(buf), "OpenCV bridge enabled (v%d.%d.%d)",
                  CV_VERSION_MAJOR, CV_VERSION_MINOR, CV_VERSION_REVISION);
    return buf;
#else
    return "OpenCV bridge unavailable";
#endif
}

}  // namespace XER::Bridge::OpenCV
