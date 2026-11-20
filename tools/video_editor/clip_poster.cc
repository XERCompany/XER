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

// Thumbnail pixel budget: the in-memory posters decoded by
// GetPosterAt feed the timeline's per-clip thumbnail strip via
// MediaPrep -> PosterCache, which is byte-bounded (64 MiB). A
// native-resolution BGRA frame costs ~8.3 MB at 1080p — the cache
// could hold only ~7 of them, while ONE clip's strip already asks
// for dozens of distinct 0.25 s buckets. The resulting LRU thrash
// re-decoded a different slot almost every paint, which is what made
// the timeline thumbnails visibly blink (kPending -> blank -> kReady
// -> evicted -> kPending ...). Capping the long edge at 320 px keeps
// each poster ~230 KB, so hundreds stay resident and a widget-level
// cache miss is answered kReady synchronously.
constexpr int kPosterMaxEdge = 320;

// Aspect-preserving BGRA area-average downscale: each destination
// pixel averages the source rectangle it covers (proper minification
// for the large ratios posters see, unlike nearest sampling).
// `src` must be BGRA8; `dst_w`/`dst_h` must be <= the source size.
bool DownscaleBgra(const video::Frame& src, int dst_w, int dst_h,
                   video::Frame* dst) {
    const int src_w = src.Width();
    const int src_h = src.Height();
    if (!src.IsValid() || src.Format() != video::PixelFormat::BGRA8 ||
        dst == nullptr || dst_w <= 0 || dst_h <= 0 ||
        dst_w > src_w || dst_h > src_h) {
        return false;
    }
    if (!dst->Allocate(dst_w, dst_h, video::PixelFormat::BGRA8)) {
        return false;
    }
    const std::uint8_t* sp = src.Data();
    std::uint8_t* dp = dst->Data();
    for (int dy = 0; dy < dst_h; ++dy) {
        const int sy0 = dy * src_h / dst_h;
        const int sy1 = (dy + 1) * src_h / dst_h;   // > sy0 (dst<=src)
        for (int dx = 0; dx < dst_w; ++dx) {
            const int sx0 = dx * src_w / dst_w;
            const int sx1 = (dx + 1) * src_w / dst_w;
            std::uint32_t b = 0, g = 0, r = 0, a = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const std::uint8_t* row =
                    sp + (static_cast<std::size_t>(sy) * src_w + sx0) *
                         4u;
                for (int sx = sx0; sx < sx1; ++sx) {
                    b += row[0];
                    g += row[1];
                    r += row[2];
                    a += row[3];
                    row += 4;
                }
            }
            const std::uint32_t n =
                static_cast<std::uint32_t>((sy1 - sy0) * (sx1 - sx0));
            std::uint8_t* out =
                dp + (static_cast<std::size_t>(dy) * dst_w + dx) * 4u;
            out[0] = static_cast<std::uint8_t>(b / n);
            out[1] = static_cast<std::uint8_t>(g / n);
            out[2] = static_cast<std::uint8_t>(r / n);
            out[3] = static_cast<std::uint8_t>(a / n);
        }
    }
    return true;
}

// Shared decode core for Extract/GetPosterAt — the decoder must be
// open and carry video (width/height > 0). Seeks keyframe-backward to
// `target` (best-effort — Seek clamps out-of-range values itself; on
// false the decoder stays near position 0 and the bounded forward loop
// below still yields a valid early frame), then forward-decodes to the
// first frame at/after the target, keeping the last decoded frame at
// EOF. The decoded BGRA pixels are copied into `frame_out`.
bool DecodeFrameAt(FFmpeg::VideoDecoder& decoder, double target,
                   video::Frame* frame_out, std::string* error_out) {
    decoder.Seek(target);   // best-effort — see above

    // Bounded forward-discard: NextFrame decodes forward from the
    // keyframe; keep the LAST successfully decoded frame and stop at
    // the first one at/after the target. At EOF (NextFrame false)
    // `kept` still holds the last decoded frame — the EOF fallback.
    // IMPORTANT: NextFrame resizes AND zero-fills its output vector
    // before decoding (ffmpeg_video_decoder.cc), so a failed call
    // leaves `pixels` zeroed — the decoded frame must be swapped
    // into `kept` on every SUCCESS, never read after a failure.
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

    // Copy the kept packed-BGRA pixels into the caller's video::Frame.
    if (!frame_out->Allocate(kept_w, kept_h, video::PixelFormat::BGRA8)) {
        return Fail(error_out, "frame allocation failed");
    }
    std::memcpy(frame_out->Data(), kept.data(), kept.size());
    return true;
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

    // 4. Decode the frame nearest the clamped seek target into memory
    //    (DecodeFrameAt seeks + forward-decodes, keeping the last frame
    //    at EOF). A wrong-position poster beats no poster; catalog can
    //    re-extract.
    const double target = std::max(0.0, seek_seconds);
    video::Frame frame;
    if (!DecodeFrameAt(decoder, target, &frame, error_out)) {
        return false;
    }

    // 5. PNG export. FrameExporter::ToFile is PNG-only (codec_name
    //    "png") and re-checks Bridge::FFmpeg::IsAvailable() itself.
    video::FrameExporter exporter;
    if (!exporter.ToFile(utf8_out_png_path, frame)) {
        return Fail(error_out,
                    "PNG export failed for " + utf8_out_png_path);
    }
    return true;
}

bool ClipPoster::GetPosterAt(const std::string& utf8_source_path,
                             double seconds,
                             video::Frame* frame_out,
                             std::string* error_out) {
    if (frame_out == nullptr) {
        return Fail(error_out, "frame_out is required");
    }
    // Same open + audio-only gate as Extract — one decode path.
    FFmpeg::VideoDecoder decoder;
    if (!decoder.Open(utf8_source_path, error_out)) {
        return false;
    }
    if (decoder.GetWidth() <= 0 || decoder.GetHeight() <= 0) {
        return Fail(error_out, "no video stream (audio-only file)");
    }

    // Out-of-range times fall back to the mid-stream frame (spec §5) —
    // negative or beyond-duration requests get the same poster the
    // catalog extracts by default. An unknown/zero duration collapses
    // the fallback to 0 (decode from the start).
    const double duration = decoder.GetDurationSeconds();
    double target = seconds;
    if (!(target >= 0.0) || (duration > 0.0 && target > duration)) {
        target = duration > 0.0 ? duration * 0.5 : 0.0;
    }
    video::Frame decoded;
    if (!DecodeFrameAt(decoder, target, &decoded, error_out)) {
        return false;
    }

    // Shrink to thumbnail size before handing the frame to the
    // byte-bounded PosterCache — see kPosterMaxEdge above. Sources at
    // or under the cap pass through untouched.
    const int w = decoded.Width(), h = decoded.Height();
    const int longest = std::max(w, h);
    if (longest <= kPosterMaxEdge) {
        *frame_out = decoded;
        return true;
    }
    const double scale =
        static_cast<double>(kPosterMaxEdge) / longest;
    const int dst_w = std::max(1, static_cast<int>(w * scale));
    const int dst_h = std::max(1, static_cast<int>(h * scale));
    if (!DownscaleBgra(decoded, dst_w, dst_h, frame_out)) {
        return Fail(error_out, "poster downscale failed");
    }
    return true;
}

}  // namespace XER::VideoEditor
