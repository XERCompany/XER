#include "wrapper/angle/angle_engine_bridge.h"

namespace XER::engine::bridge::angle {

bool IsAvailable() {
#if ENGINE_HAS_ANGLE_BRIDGE
    return true;
#else
    return false;
#endif
}

std::string Summary() {
    return IsAvailable() ? "ANGLE bridge enabled" : "ANGLE bridge unavailable";
}

}  // namespace XER::engine::bridge::angle