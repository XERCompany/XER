#include "image_saver.h"
#include "helper/string_utils.h"
#include "helper/path_utils.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace image {

namespace {

int GuessBytesPerPixel(const std::string& pixel_format) {
    const std::string fmt = XER::Helper::String::ToLowerAscii(pixel_format);
    if (fmt == "rgba" || fmt == "bgra" || fmt == "argb" || fmt == "abgr") return 4;
    if (fmt == "rgb" || fmt == "bgr") return 3;
    if (fmt == "gray" || fmt == "gray8" || fmt == "y") return 1;
    return 0;
}

void EnsureFrameLineSizes(XER::Bridge::FFmpeg::VideoFrameInfo* frame) {
    if (frame == nullptr || !frame->line_sizes.empty()) return;
    if (frame->width <= 0 || frame->height <= 0) return;

    const int bpp = GuessBytesPerPixel(frame->pixel_format);
    if (bpp > 0) {
        frame->line_sizes = {frame->width * bpp};
        return;
    }

    // Fallback for unknown formats: infer one-plane stride from payload size.
    if (!frame->data.empty()) {
        const size_t per_row = frame->data.size() / static_cast<size_t>(frame->height);
        if (per_row > 0) {
            frame->line_sizes = {static_cast<int>(per_row)};
        }
    }
}

}  // namespace

bool ImageSaver::Save(const ImageBuffer& img, const std::string& path, int quality) {
    const std::string upath = XER::Helper::Path::NormalizeUtf8Path(path);
    const std::string ext = XER::Helper::String::ToLowerAscii(
        XER::Helper::Path::ToUtf8(
            XER::Helper::Path::FromUtf8(upath).extension()));
    if (ext == ".jpg" || ext == ".jpeg") return SaveJpeg(img, upath, quality);
    if (ext == ".raw")  return SaveRaw(img, upath);
    return SavePng(img, upath);  // default
}

bool ImageSaver::SavePng(const ImageBuffer& img, const std::string& path) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) return false;
    if (!img.IsValid()) return false;
    const std::string upath = XER::Helper::Path::NormalizeUtf8Path(path);
    int w = img.Width(), h = img.Height();
    // Build VideoFrameInfo-compatible frame
    XER::Bridge::FFmpeg::EncodeVideoParams p;
    p.output_path  = upath;
    p.codec_name   = "png";
    p.width        = w;
    p.height       = h;
    p.fps_num      = 1;
    p.fps_den      = 1;
    p.pixel_format = "rgba";

    XER::Bridge::FFmpeg::VideoFrameInfo fr;
    fr.width  = w; fr.height = h;
    fr.pixel_format = "rgba";
    fr.data.assign(img.Data(), img.Data() + img.DataSize());
    EnsureFrameLineSizes(&fr);

    std::string err;
    if (!XER::Bridge::FFmpeg::EncodeVideoFrames(p, {fr}, &err)) {
        XER::Flux::Core::Logger().Error("ImageSaver", "PNG save failed: " + err);
        return false;
    }
    return true;
}

bool ImageSaver::SaveJpeg(const ImageBuffer& img, const std::string& path,
                           int quality) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) return false;
    if (!img.IsValid()) return false;
    const std::string upath = XER::Helper::Path::NormalizeUtf8Path(path);
    int w = img.Width(), h = img.Height();
    XER::Bridge::FFmpeg::EncodeVideoParams p;
    p.output_path  = upath;
    p.codec_name   = "mjpeg";
    p.width        = w;
    p.height       = h;
    p.fps_num      = 1;
    p.fps_den      = 1;
    p.pixel_format = "rgba";
    (void)quality;

    XER::Bridge::FFmpeg::VideoFrameInfo fr;
    fr.width=w; fr.height=h;
    fr.pixel_format="rgba";
    fr.data.assign(img.Data(), img.Data()+img.DataSize());
    EnsureFrameLineSizes(&fr);

    std::string err;
    if (!XER::Bridge::FFmpeg::EncodeVideoFrames(p, {fr}, &err)) {
        XER::Flux::Core::Logger().Error("ImageSaver", "JPEG save failed: " + err);
        return false;
    }
    return true;
}

bool ImageSaver::SaveRaw(const ImageBuffer& img, const std::string& path) {
    const std::string upath = XER::Helper::Path::NormalizeUtf8Path(path);
    std::ofstream f(XER::Helper::Path::FromUtf8(upath), std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(img.Data()), img.DataSize());
    return f.good();
}

}  // namespace image
