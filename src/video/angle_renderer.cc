#include "angle_renderer.h"
#include "video_utils.h"
#include "wrapper/angle/angle_engine_bridge.h"
#include "flux/core/logger.h"

namespace video {

static flux::core::Logger& Log() {
    static flux::core::Logger logger;
    return logger;
}

AngleRenderer::~AngleRenderer() { Shutdown(); }

bool AngleRenderer::Initialize(const std::string& /*device_hint*/) {
    bool angle_ok = engine::bridge::angle::IsAvailable();
    Log().Info("AngleRenderer",
               std::string("ANGLE: ") +
               (angle_ok ? "available" : "not available") +
               " | " + engine::bridge::angle::Summary());
    ready_ = angle_ok;
    return ready_;
}

void AngleRenderer::Shutdown() {
    if (ready_) {
        Log().Info("AngleRenderer", "Shutdown");
        ready_ = false;
    }
}

bool AngleRenderer::Render(const Frame& src, RenderTarget& target) {
    if (!ready_ || !src.IsValid()) return false;
    Frame* dst = target.GetFrame();
    if (!dst || !dst->IsValid()) return false;

    if (utils::CopyFramePixels(src, *dst)) return true;
    return utils::ResizeFrameWithSkia(src, *dst);
}

}  // namespace video