// clip_poster.cc — see clip_poster.h for the contract. No platform
// ifdefs needed: off-Windows VideoDecoder::Open is a stub returning
// false (step 2 fails cleanly) and FrameExporter::ToFile re-checks
// Bridge::FFmpeg::IsAvailable() itself.
#include "clip_poster.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#include "frame.h"
#include "frame_exporter.h"
#include "helper/path_utils.h"
#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;              // UTF-8 <-> fs::path
namespace FFmpeg = XER::Bridge::FFmpeg;

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

}  // namespace

bool ClipPoster::Extract(const std::string& utf8_source_path,
                         double seek_seconds,
                         const std::string& utf8_out_png_path,
                         std::string* error_out) {
    // 1. Create parent dirs of the output path. Best-effort with
    //    error_code (never throws); a real failure surfaces at export.
    const std::filesystem::path out_path =
        Path::FromUtf8(utf8_out_png_path);
    const std::filesystem::path parent = out_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    // 2. Open the streaming decoder. RAII: ~VideoDecoder calls Close().
    //    error_out is passed straight through — Open reports e.g.
    //    "cannot open: <path>" for missing/unreadable files.
    FFmpeg::VideoDecoder decoder;
    if (!decoder.Open(utf8_source_path, error_out)) {
        return false;
    }

    // 3. Audio-only containers open successfully but carry no pictures.
    if (decoder.GetWidth() <= 0 || decoder.GetHeight() <= 0) {
        return Fail(error_out, "no video stream (audio-only file)");
    }

    // 4. Seek keyframe-backward — BEST-EFFORT. Seek clamps out-of-range
    //    values to the stream bounds itself; on false (containers that
    //    refuse seeks) the decoder stays near position 0 and the
    //    bounded forward loop below still yields a valid early frame.
    //    A wrong-position poster beats no poster; catalog can re-extract.
    const double target = std::max(0.0, seek_seconds);
    decoder.Seek(target);

    // 5. Bounded forward-discard: NextFrame decodes forward from the
    //    keyframe; keep the LAST successfully decoded frame and stop at
    //    the first one at/after the target. At EOF (NextFrame false)
    //    `kept` still holds the last decoded frame — the EOF fallback.
    //    IMPORTANT: NextFrame resizes AND zero-fills its output vector
    //    before decoding (ffmpeg_video_decoder.cc), so a failed call
    //    leaves `pixels` zeroed — the decoded frame must be swapped
    //    into `kept` on every SUCCESS, never read after a failure.
    constexpr int kMaxForwardFrames = 300;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> kept;
    int width = 0;
    int height = 0;
    int kept_w = 0;
    int kept_h = 0;
    for (int i = 0; i < kMaxForwardFrames &&
                    decoder.NextFrame(pixels, width, height);
         ++i) {
        pixels.swap(kept);  // kept = just-decoded frame; pixels = scratch
        kept_w = width;
        kept_h = height;
        if (decoder.GetPositionSeconds() >= target) {
            break;
        }
    }
    if (kept_w <= 0 || kept_h <= 0 || kept.empty()) {
        return Fail(error_out, "no frames decoded");
    }
    const std::size_t expected_bytes =
        static_cast<std::size_t>(kept_w) *
        static_cast<std::size_t>(kept_h) * 4u;
    if (kept.size() != expected_bytes) {
        return Fail(error_out, "decoded frame size mismatch");
    }

    // 6. Copy the kept packed-BGRA pixels into a video::Frame.
    video::Frame frame;
    if (!frame.Allocate(kept_w, kept_h, video::PixelFormat::BGRA8)) {
        return Fail(error_out, "frame allocation failed");
    }
    std::memcpy(frame.Data(), kept.data(), kept.size());

    // 7. PNG export. FrameExporter::ToFile is PNG-only (codec_name
    //    "png") and re-checks Bridge::FFmpeg::IsAvailable() itself.
    video::FrameExporter exporter;
    if (!exporter.ToFile(utf8_out_png_path, frame)) {
        return Fail(error_out,
                    "PNG export failed for " + utf8_out_png_path);
    }
    return true;
}

}  // namespace XER::VideoEditor
