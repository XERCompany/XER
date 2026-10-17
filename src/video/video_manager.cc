#include "video_manager.h"
#include "video_utils.h"
#include "cuda_renderer.h"
#include "vulkan_renderer.h"
#include "angle_renderer.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "wrapper/skia/skia_engine_bridge.h"
#if ENGINE_HAS_CUDA_BRIDGE
#include "wrapper/cuda/cuda_engine_bridge.h"
#endif
#include "flux/core/logger.h"

namespace video {

static flux::core::Logger& Log() {
    static flux::core::Logger logger;
    return logger;
}

VideoManager::VideoManager(VideoConfiguration cfg)
    : cfg_(std::move(cfg)),
      frame_cache_(cfg_.max_cache_frames),
      fps_controller_(cfg_.target_fps) {}

VideoManager::~VideoManager() { Shutdown(); }

bool VideoManager::InitRenderer(RendererType type, const std::string& device_hint) {
    // Auto-select best available renderer if not forced
    if (type == RendererType::CUDA
#if ENGINE_HAS_CUDA_BRIDGE
        && !engine::bridge::cuda::IsAvailable()
#else
        && true
#endif
    ) {
        Log().Warning("VideoManager",
                      "CUDA requested but not available; falling back to Vulkan");
        type = RendererType::Vulkan;
    }

    switch (type) {
        case RendererType::CUDA:   renderer_ = std::make_unique<CudaRenderer>();   break;
        case RendererType::Angle:  renderer_ = std::make_unique<AngleRenderer>();  break;
        case RendererType::CPU:    // fallthrough to Vulkan (CPU path inside)
        case RendererType::Vulkan: renderer_ = std::make_unique<VulkanRenderer>(); break;
    }

    bool ok = renderer_->Initialize(device_hint);
    Log().Info("VideoManager",
               std::string("Renderer ") + (ok ? "ready" : "FAILED") +
               " | FFmpeg: " + (engine::bridge::ffmpeg::IsAvailable() ? "yes" : "no") +
               " | Skia: " + (engine::bridge::skia::IsAvailable() ? "yes" : "no"));
    return ok;
}

void VideoManager::Shutdown() {
    Log().Info("VideoManager", "Shutting down...");
    for (auto& [id, s] : streams_) s->Stop();
    streams_.clear();
    if (renderer_) renderer_->Shutdown();
    Log().Info("VideoManager", "Shutdown complete");
}

VideoStream* VideoManager::CreateStream(const std::string& id) {
    auto [it, ok] = streams_.emplace(id, std::make_unique<VideoStream>(id));
    Log().Debug("VideoManager", "Stream created: " + id);
    return it->second.get();
}

VideoStream* VideoManager::GetStream(const std::string& id) {
    auto it = streams_.find(id);
    return (it != streams_.end()) ? it->second.get() : nullptr;
}

void VideoManager::RemoveStream(const std::string& id) {
    streams_.erase(id);
    Log().Debug("VideoManager", "Stream removed: " + id);
}

bool VideoManager::ProcessFrame(Frame& out, const std::string& stream_id) {
    VideoStream* stream = GetStream(stream_id);
    if (!stream) {
        Log().Error("VideoManager", "ProcessFrame: unknown stream '" + stream_id + "'");
        return false;
    }

    auto frame = stream->NextFrame();
    if (!frame) return false;  // no frame yet (FPS throttle or EOF)

    if (renderer_ && renderer_->IsReady()) {
        RenderTarget rt(frame->Width(), frame->Height(), frame->Format());
        if (!renderer_->Render(*frame, rt)) {
            Log().Warning("VideoManager", "Render failed for stream '" + stream_id + "'");
            return false;
        }
        if (Frame* rf = rt.GetFrame(); rf && rf->IsValid()) {
            out.Allocate(rf->Width(), rf->Height(), rf->Format());
            return utils::CopyFramePixels(*rf, out);
        }
    } else {
        out.Allocate(frame->Width(), frame->Height(), frame->Format());
        return utils::CopyFramePixels(*frame, out);
    }
    return false;
}

}  // namespace video