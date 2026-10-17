#include "frame_exporter.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"

#include <utility>

namespace video {

static flux::core::Logger& Log() {
    static flux::core::Logger logger;
    return logger;
}

FrameExporter::FrameExporter(FrameCache* cache) : cache_(cache) {}

bool FrameExporter::ToCache(const std::string& key, std::shared_ptr<Frame> frame) {
    if (!cache_ || !frame) return false;
    cache_->Store(key, std::move(frame));
    return true;
}

bool FrameExporter::ToFile(const std::string& path, const Frame& frame) {
    if (!engine::bridge::ffmpeg::IsAvailable()) {
        Log().Error("FrameExporter", "FFmpeg bridge not available");
        return false;
    }
    if (!frame.IsValid()) {
        Log().Error("FrameExporter", "Invalid frame passed to ToFile");
        return false;
    }

    // Build a VideoFrameInfo and encode as a single-frame video / image
    engine::bridge::ffmpeg::VideoFrameInfo vf;
    vf.stream_index = 0;
    vf.width  = frame.Width();
    vf.height = frame.Height();
    vf.pixel_format = "bgra";
    vf.pts    = frame.Pts();
    vf.key_frame = true;
    vf.line_sizes.push_back(frame.Width() * 4);
    vf.data.assign(frame.Data(), frame.Data() + frame.DataSize());

    engine::bridge::ffmpeg::EncodeVideoParams params;
    params.output_path  = path;
    params.codec_name   = "png";    // lossless single-frame
    params.width        = frame.Width();
    params.height       = frame.Height();
    params.pixel_format = "bgra";
    params.fps_num      = 1;
    params.fps_den      = 1;

    std::string err;
    bool ok = engine::bridge::ffmpeg::EncodeVideoFrames(params, {vf}, &err);
    if (!ok) Log().Error("FrameExporter", "EncodeVideoFrames failed: " + err);
    else     Log().Info("FrameExporter", "Saved frame to " + path);
    return ok;
}

bool FrameExporter::ExportFrames(const std::string& path,
                                   const std::vector<std::shared_ptr<Frame>>& frames,
                                   const std::string& codec, int fps) {
    if (!engine::bridge::ffmpeg::IsAvailable() || frames.empty()) return false;

    std::vector<engine::bridge::ffmpeg::VideoFrameInfo> vframes;
    vframes.reserve(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& f = frames[i];
        if (!f || !f->IsValid()) continue;
        engine::bridge::ffmpeg::VideoFrameInfo vf;
        vf.stream_index = 0;
        vf.width  = f->Width();
        vf.height = f->Height();
        vf.pixel_format = "bgra";
        vf.pts    = f->Pts();
        vf.key_frame = (i == 0);
        vf.line_sizes.push_back(f->Width() * 4);
        vf.data.assign(f->Data(), f->Data() + f->DataSize());
        vframes.push_back(std::move(vf));
    }

    engine::bridge::ffmpeg::EncodeVideoParams params;
    params.output_path  = path;
    params.codec_name   = codec;
    params.width        = frames[0]->Width();
    params.height       = frames[0]->Height();
    params.pixel_format = "bgra";
    params.fps_num      = fps;
    params.fps_den      = 1;

    std::string err;
    bool ok = engine::bridge::ffmpeg::EncodeVideoFrames(params, vframes, &err);
    if (!ok) Log().Error("FrameExporter", "ExportFrames failed: " + err);
    else     Log().Info("FrameExporter",
                        "Exported " + std::to_string(vframes.size()) + " frames to " + path);
    return ok;
}

bool FrameExporter::ExportFrameStream(const std::string& path,
                                      size_t frame_count,
                                      int width,
                                      int height,
                                      const std::string& codec,
                                      int fps,
                                      const FrameStreamProvider& provider) {
    if (!engine::bridge::ffmpeg::IsAvailable() || frame_count == 0u || !provider) return false;
    if (width <= 0 || height <= 0) return false;

    engine::bridge::ffmpeg::EncodeVideoParams params;
    params.output_path = path;
    params.codec_name = codec;
    params.width = width;
    params.height = height;
    params.pixel_format = "bgra";
    params.fps_num = fps;
    params.fps_den = 1;

    std::string err;
    bool ok = engine::bridge::ffmpeg::EncodeVideoFrameStream(
        params,
        frame_count,
        [&provider](size_t frame_index, engine::bridge::ffmpeg::VideoFrameInfo* out_frame, std::string* error_out) {
            if (out_frame == nullptr) {
                if (error_out != nullptr) *error_out = "stream frame target is null";
                return false;
            }
            std::shared_ptr<Frame> frame;
            if (!provider(frame_index, &frame, error_out) || !frame || !frame->IsValid()) {
                if (error_out != nullptr && error_out->empty()) *error_out = "stream frame provider returned an invalid frame";
                return false;
            }

            out_frame->stream_index = 0;
            out_frame->width = frame->Width();
            out_frame->height = frame->Height();
            out_frame->pixel_format = "bgra";
            out_frame->pts = frame->Pts();
            out_frame->key_frame = frame_index == 0u;
            out_frame->line_sizes.push_back(frame->Width() * 4);
            out_frame->data.assign(frame->Data(), frame->Data() + frame->DataSize());
            return true;
        },
        &err);
    if (!ok) Log().Error("FrameExporter", "ExportFrameStream failed: " + err);
    else Log().Info("FrameExporter", "Stream exported " + std::to_string(frame_count) + " frames to " + path);
    return ok;
}

}  // namespace video