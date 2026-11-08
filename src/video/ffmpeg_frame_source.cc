#include "ffmpeg_frame_source.h"
#include "flux/core/logger.h"
#include <cstring>
#include <algorithm>

namespace video {

// ─── helpers ────────────────────────────────────────────────────────────────

static flux::core::Logger& Log() {
    static flux::core::Logger logger;
    return logger;
}

/// YUV420P (plane-packed) → BGRA8
static void Yuv420pToBgra(const uint8_t* y_plane, const uint8_t* u_plane,
                            const uint8_t* v_plane,
                            int y_stride, int uv_stride,
                            int w, int h, uint8_t* dst) {
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            int Y = y_plane[row * y_stride + col];
            int U = u_plane[(row / 2) * uv_stride + (col / 2)] - 128;
            int V = v_plane[(row / 2) * uv_stride + (col / 2)] - 128;
            int R = std::clamp(Y + 1402 * V / 1000,                0, 255);
            int G = std::clamp(Y - 344  * U / 1000 - 714 * V / 1000, 0, 255);
            int B = std::clamp(Y + 1772 * U / 1000,                0, 255);
            uint8_t* px = dst + (row * w + col) * 4;
            px[0] = static_cast<uint8_t>(B);
            px[1] = static_cast<uint8_t>(G);
            px[2] = static_cast<uint8_t>(R);
            px[3] = 255;
        }
    }
}

/// RGB24 → BGRA8
static void Rgb24ToBgra(const uint8_t* src, int stride, int w, int h, uint8_t* dst) {
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            const uint8_t* s = src + row * stride + col * 3;
            uint8_t* d = dst + (row * w + col) * 4;
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;  // BGR → BGRA
        }
    }
}

/// BGR24 → BGRA8
static void Bgr24ToBgra(const uint8_t* src, int stride, int w, int h, uint8_t* dst) {
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            const uint8_t* s = src + row * stride + col * 3;
            uint8_t* d = dst + (row * w + col) * 4;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
        }
    }
}

/// NV12 (Y + interleaved UV) → BGRA8
static void Nv12ToBgra(const uint8_t* y_plane, const uint8_t* uv_plane,
                         int y_stride, int uv_stride,
                         int w, int h, uint8_t* dst) {
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            int Y = y_plane[row * y_stride + col];
            int U = uv_plane[(row / 2) * uv_stride + (col & ~1)]     - 128;
            int V = uv_plane[(row / 2) * uv_stride + (col & ~1) + 1] - 128;
            int R = std::clamp(Y + 1402 * V / 1000,                0, 255);
            int G = std::clamp(Y - 344  * U / 1000 - 714 * V / 1000, 0, 255);
            int B = std::clamp(Y + 1772 * U / 1000,                0, 255);
            uint8_t* px = dst + (row * w + col) * 4;
            px[0] = static_cast<uint8_t>(B);
            px[1] = static_cast<uint8_t>(G);
            px[2] = static_cast<uint8_t>(R);
            px[3] = 255;
        }
    }
}

// ─── FFmpegFrameSource ───────────────────────────────────────────────────────

bool FFmpegFrameSource::Open(const std::string& uri) {
    using namespace XER::Bridge::FFmpeg;

    if (!IsAvailable()) {
        Log().Error("FFmpegFrameSource", "FFmpeg bridge not available");
        return false;
    }

    Close();
    uri_ = uri;
    MediaInfo info;
    std::string err;
    if (!ProbeMedia(uri, &info, &err)) {
        Log().Error("FFmpegFrameSource", "ProbeMedia failed: " + err);
        return false;
    }

    for (const auto& s : info.streams) {
        if (s.media_type == "video") {
            stream_idx_    = s.index;
            width_         = s.width;
            height_        = s.height;
            total_frames_  = s.frame_count;
            // derive FPS: codec reports time_base; use fps from stream
            if (s.time_base_num > 0 && s.time_base_den > 0) {
                // For video streams time_base is typically 1/fps or 1/90000
                // Use frame_count / duration as fallback
                if (s.duration > 0 && total_frames_ > 0) {
                    double dur_sec = static_cast<double>(s.duration) *
                                     s.time_base_num / s.time_base_den;
                    fps_ = (dur_sec > 0) ? total_frames_ / dur_sec : 30.0;
                } else {
                    fps_ = 30.0;
                }
            }
            break;
        }
    }

    if (stream_idx_ < 0) {
        Log().Error("FFmpegFrameSource", "No video stream found in: " + uri);
        return false;
    }

    is_open_  = true;
    decoded_  = false;
    Log().Info("FFmpegFrameSource",
               "Opened " + uri + " [" + std::to_string(width_) + "x" +
               std::to_string(height_) + " @ " +
               std::to_string(static_cast<int>(fps_)) + " fps]");
    return true;
}

void FFmpegFrameSource::Close() {
    is_open_ = false;
    decoded_ = false;
    queue_.clear();
}

bool FFmpegFrameSource::DecodeAll() {
    using namespace XER::Bridge::FFmpeg;
    std::vector<VideoFrameInfo> raw;
    std::string err;
    int max_frames = (total_frames_ > 0) ? static_cast<int>(total_frames_) : 4096;
    if (!DecodeVideoFrames(uri_, stream_idx_, max_frames, &raw, &err)) {
        Log().Error("FFmpegFrameSource", "DecodeVideoFrames failed: " + err);
        return false;
    }
    for (const auto& vf : raw) {
        auto f = MakeFrameFromInfo(vf);
        if (f) queue_.push_back(std::move(f));
    }
    decoded_ = true;
    Log().Info("FFmpegFrameSource",
               "Decoded " + std::to_string(queue_.size()) + " frames from " + uri_);
    return !queue_.empty();
}

std::shared_ptr<Frame> FFmpegFrameSource::MakeFrameFromInfo(
        const XER::Bridge::FFmpeg::VideoFrameInfo& info) {
    if (info.width <= 0 || info.height <= 0 || info.data.empty()) return nullptr;

    auto frame = std::make_shared<Frame>();
    if (!frame->Allocate(info.width, info.height, PixelFormat::BGRA8)) return nullptr;
    frame->SetPts(info.pts);
    frame->Meta().fps = fps_;
    frame->Meta().is_keyframe = info.key_frame;

    uint8_t* dst = frame->Data();
    const uint8_t* src = info.data.data();
    int w = info.width, h = info.height;
    const std::string& fmt = info.pixel_format;

    if (fmt == "bgra" || fmt == "bgra8") {
        // Packed BGRA: stride may differ from width*4
        int stride = (info.line_sizes.size() > 0) ? info.line_sizes[0] : w * 4;
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + row * w * 4, src + row * stride, w * 4);

    } else if (fmt == "rgba" || fmt == "rgba8") {
        int stride = (info.line_sizes.size() > 0) ? info.line_sizes[0] : w * 4;
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                const uint8_t* s = src + row * stride + col * 4;
                uint8_t* d = dst + (row * w + col) * 4;
                d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];  // RGBA→BGRA
            }
        }
    } else if (fmt == "rgb24") {
        int stride = (info.line_sizes.size() > 0) ? info.line_sizes[0] : w * 3;
        Rgb24ToBgra(src, stride, w, h, dst);

    } else if (fmt == "bgr24") {
        int stride = (info.line_sizes.size() > 0) ? info.line_sizes[0] : w * 3;
        Bgr24ToBgra(src, stride, w, h, dst);

    } else if (fmt == "yuv420p" || fmt == "yuvj420p") {
        int y_stride  = (info.line_sizes.size() > 0) ? info.line_sizes[0] : w;
        int uv_stride = (info.line_sizes.size() > 1) ? info.line_sizes[1] : w / 2;
        int y_size    = h * y_stride;
        int u_size    = (h / 2) * uv_stride;
        if (static_cast<size_t>(y_size + u_size) > info.data.size()) return nullptr;
        Yuv420pToBgra(src, src + y_size, src + y_size + u_size,
                      y_stride, uv_stride, w, h, dst);

    } else if (fmt == "nv12") {
        int y_stride  = (info.line_sizes.size() > 0) ? info.line_sizes[0] : w;
        int uv_stride = (info.line_sizes.size() > 1) ? info.line_sizes[1] : w;
        int y_size    = h * y_stride;
        if (static_cast<size_t>(y_size) > info.data.size()) return nullptr;
        Nv12ToBgra(src, src + y_size, y_stride, uv_stride, w, h, dst);

    } else {
        Log().Warning("FFmpegFrameSource",
                      "Unsupported pixel format: " + fmt + " — frame dropped");
        return nullptr;
    }

    return frame;
}

std::shared_ptr<Frame> FFmpegFrameSource::NextFrame() {
    if (!is_open_) return nullptr;
    if (!decoded_ && !DecodeAll()) return nullptr;
    if (queue_.empty()) return nullptr;
    auto f = queue_.front();
    queue_.pop_front();
    return f;
}

}  // namespace video
