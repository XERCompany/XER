#include "cuda_renderer.h"
#include "video_utils.h"

#if ENGINE_HAS_CUDA_BRIDGE
#include "wrapper/cuda/cuda_engine_bridge.h"
#endif
#include "flux/core/logger.h"

namespace video {

static XER::Flux::Core::Logger& Log() {
    static XER::Flux::Core::Logger logger;
    return logger;
}

CudaRenderer::~CudaRenderer() { Shutdown(); }

bool CudaRenderer::Initialize(const std::string& device_hint) {
#if ENGINE_HAS_CUDA_BRIDGE
    if (!XER::Bridge::Cuda::IsAvailable()) {
        Log().Warning("CudaRenderer", "CUDA not available: " +
                      XER::Bridge::Cuda::Summary());
        return false;
    }
    device_hint_ = device_hint;
    ready_ = true;
    Log().Info("CudaRenderer", "Initialized. " + XER::Bridge::Cuda::Summary());
    return true;
#else
    Log().Warning("CudaRenderer", "CUDA bridge disabled by ENABLE_CUDA=OFF");
    return false;
#endif
}

void CudaRenderer::Shutdown() {
    if (ready_) {
        Log().Info("CudaRenderer", "Shutdown");
        ready_ = false;
    }
}

bool CudaRenderer::Render(const Frame& src, RenderTarget& target) {
    if (!ready_ || !src.IsValid()) return false;
    Frame* dst = target.GetFrame();
    if (!dst || !dst->IsValid()) return false;

    if (utils::CopyFramePixels(src, *dst)) return true;
    return utils::ResizeFrameWithSkia(src, *dst);
}

}  // namespace video