// Implementation of ai/ai_bounce.h — the decode->transform->encode
// bounce. Two decode passes: pass A counts the span's source frames
// (and drains the audio tap into chunks when whole-file audio is
// wanted); pass B re-decodes while the encode stream's video
// provider pulls output frames through the caller's transform.
// Keeps peak video memory at a handful of frames + the collected
// audio (capped).
#include "ai/ai_bounce.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include "cache/cache_utils.h"
#include "helper/path_utils.h"
#include "media_decoder.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace FFmpeg = XER::Bridge::FFmpeg;
namespace Path = XER::Helper::Path;

// Whole decoded-audio cap for the bounce (~9 min of 48 kHz fltp
// stereo). Over it the bounce goes video-only instead of
// ballooning — has_audio reports the truth.
constexpr std::size_t kAudioCapBytes = 512ull * 1024 * 1024;

// Position-epsilon for span edges: a quarter frame at the probed
// fps keeps pts jitter from shifting the cut by a frame.
inline double FrameEps(double fps) {
    return fps > 0.0 ? 0.25 / fps : 1.0e-3;
}

}  // namespace

std::string AiBounceOutputPath(const std::string& source_utf8,
                               const std::string& tag) {
    const std::filesystem::path src = Path::FromUtf8(source_utf8);
    std::filesystem::path dir = src.parent_path();
    const std::string stem = src.stem().string();
    const std::string suffix =
        ".xer." + (tag.empty() ? std::string("out") : tag) + ".mp4";
    std::filesystem::path candidate = dir / (stem + suffix);
    int n = 2;
    std::error_code ec;
    while (std::filesystem::exists(candidate, ec)) {
        candidate = dir / (stem + ".xer." +
                           (tag.empty() ? std::string("out") : tag) +
                           "." + std::to_string(n) + ".mp4");
        ++n;
    }
    return Path::ToUtf8(candidate);
}

AiBounceFrameFn MakeFrameTransform(AiFrameFn fn) {
    return [fn = std::move(fn)](std::size_t /*i*/,
                                const AiBounceContext& ctx,
                                std::vector<std::uint8_t>* out,
                                int* w, int* h,
                                std::string* err) -> bool {
        AiBounceFrame src;
        if (!ctx.next_source(&src, err)) {
            if (err != nullptr && err->empty()) {
                *err = "source ended before all outputs were "
                       "produced";
            }
            return false;
        }
        return fn(src, out, w, h, err);
    };
}

AiBounceFrameFn MakePairInterpTransform(int factor,
                                        AiPairInterpFn interp) {
    struct State {
        std::deque<AiBounceFrame> window;
        std::size_t base = 0;       // source index of window.front()
        std::size_t pulled = 0;     // next source index to pull
    };
    auto state = std::make_shared<State>();
    return [factor, interp = std::move(interp), state](
               std::size_t i, const AiBounceContext& ctx,
               std::vector<std::uint8_t>* out, int* w, int* h,
               std::string* err) -> bool {
        const int f = std::max(1, factor);
        const std::size_t p =
            static_cast<std::size_t>(i) / static_cast<std::size_t>(f);
        const int k = static_cast<int>(
            static_cast<std::size_t>(i) %
            static_cast<std::size_t>(f));
        const std::size_t need = p + (k > 0 ? 2 : 1);
        while (state->pulled < need) {
            AiBounceFrame src;
            if (!ctx.next_source(&src, err)) {
                if (err != nullptr && err->empty()) {
                    *err = "source ended before all outputs were "
                           "produced";
                }
                return false;
            }
            state->window.push_back(std::move(src));
            ++state->pulled;
        }
        while (state->base < p && !state->window.empty()) {
            state->window.pop_front();
            ++state->base;
        }
        if (state->base != p ||
            state->window.size() < (k > 0 ? 2u : 1u)) {
            if (err) *err = "pair window lost its frames";
            return false;
        }
        const AiBounceFrame& a = state->window[0];
        if (k == 0) {
            out->assign(a.bgra.begin(), a.bgra.end());
            if (w != nullptr) *w = a.w;
            if (h != nullptr) *h = a.h;
            return true;
        }
        const AiBounceFrame& b = state->window[1];
        const float t = static_cast<float>(k) / static_cast<float>(f);
        if (!interp(a, b, t, out, err)) return false;
        if (w != nullptr) *w = a.w;
        if (h != nullptr) *h = a.h;
        return true;
    };
}

bool RunVideoBounce(const AiBounceParams& params,
                    const AiBounceFrameFn& transform,
                    const std::function<void(float)>& progress,
                    const std::function<bool()>& should_cancel,
                    AiBounceResult* result, std::string* error_out) {
    const auto fail = [&](const std::string& e) {
        if (error_out) *error_out = e;
        return false;
    };
    if (result != nullptr) *result = AiBounceResult{};
    if (params.source_path.empty() || params.output_path.empty()) {
        return fail("bounce needs source and output paths");
    }
    if (transform == nullptr) {
        return fail("bounce needs a frame transform");
    }
    const int factor = std::max(1, params.out_frames_per_in);
    if (!FFmpeg::IsAvailable()) {
        return fail("FFmpeg unavailable — cannot encode the bounce");
    }

    // ---- pass A: probe + count the span (+ audio collect) -------
    std::vector<FFmpeg::AudioFrameInfo> audio_chunks;
    std::size_t audio_bytes = 0;
    bool audio_dropped = false;
    std::size_t src_total = 0;
    double fps = 0.0, span_in = 0.0, span_out = 0.0;
    int src_w = 0, src_h = 0;
    {
        MediaDecoderPtr decoder = MakeBridgeDecoder();
        if (decoder == nullptr) return fail("no media decoder");
        std::string err;
        const bool whole =
            params.in_seconds <= 0.0 && params.out_seconds <= 0.0;
        if (params.with_audio && whole) {
            decoder->EnableAudioTap(true);
        }
        if (!decoder->Open(params.source_path, &err)) {
            return fail(err.empty() ? "decoder open failed" : err);
        }
        src_w = decoder->GetWidth();
        src_h = decoder->GetHeight();
        if (src_w <= 0 || src_h <= 0) {
            decoder->Close();
            return fail("media has no video stream");
        }
        fps = decoder->GetFps();
        const double dur = decoder->GetDurationSeconds();
        span_in = std::max(0.0, params.in_seconds);
        span_out = params.out_seconds > 0.0
                       ? params.out_seconds
                       : (dur > 0.0 ? dur : 1.0e9);
        if (span_out <= span_in) {
            decoder->Close();
            return fail("empty bounce span");
        }
        const double eps = FrameEps(fps);
        // Keyframe-backward seek to the span start, then
        // forward-filter by decoded pts (pre-span frames are
        // skipped by the position checks, same pattern as
        // ai/smart_reframe.cc's probe loop).
        if (span_in > 0.0) decoder->Seek(span_in);
        std::vector<std::uint8_t> bgra;
        int w = 0, h = 0;
        std::vector<FFmpeg::AudioFrameInfo> chunk;
        int guard = 0;
        while (guard++ < 200000) {
            if (should_cancel != nullptr && should_cancel()) {
                decoder->Close();
                return fail("cancelled");
            }
            if (!decoder->NextFrame(&bgra, &w, &h)) break;
            const double pos = decoder->GetPositionSeconds();
            if (pos < span_in - eps) continue;   // pre-span
            if (pos >= span_out - eps) break;    // past the span
            ++src_total;
            if (decoder->HasAudio() && !audio_dropped) {
                chunk.clear();
                decoder->ReadAudioFrames(&chunk, 64);
                for (const FFmpeg::AudioFrameInfo& c : chunk) {
                    audio_bytes += c.data.size();
                    audio_chunks.push_back(c);
                }
                if (audio_bytes > kAudioCapBytes) {
                    audio_chunks.clear();
                    audio_chunks.shrink_to_fit();
                    audio_dropped = true;
                }
            }
        }
        // Final tap drain after the pump loop.
        if (!audio_dropped) {
            chunk.clear();
            decoder->ReadAudioFrames(&chunk, 256);
            for (const FFmpeg::AudioFrameInfo& c : chunk) {
                audio_bytes += c.data.size();
                audio_chunks.push_back(c);
            }
            if (audio_bytes > kAudioCapBytes) {
                audio_chunks.clear();
                audio_chunks.shrink_to_fit();
                audio_dropped = true;
            }
        }
        decoder->Close();
    }
    if (src_total == 0) return fail("empty bounce span");
    if (factor > 1 && src_total < 2) {
        return fail("interpolation needs at least 2 source frames");
    }
    const std::size_t out_total =
        factor > 1 ? (src_total - 1) *
                             static_cast<std::size_t>(factor) +
                         1
                   : src_total;
    // Keep only well-formed, layout-consistent audio chunks.
    {
        int rate = 0, ch = 0;
        std::string fmt;
        bool planar = false;
        bool consistent = true;
        for (const FFmpeg::AudioFrameInfo& c : audio_chunks) {
            if (c.sample_rate <= 0 || c.channels <= 0 ||
                c.sample_count <= 0) {
                consistent = false;
                break;
            }
            if (rate == 0) {
                rate = c.sample_rate;
                ch = c.channels;
                fmt = c.sample_format;
                planar = c.planar;
            } else if (c.sample_rate != rate || c.channels != ch ||
                       c.sample_format != fmt || c.planar != planar) {
                consistent = false;
                break;
            }
        }
        if (!consistent) {
            audio_chunks.clear();
            audio_dropped = true;
        }
    }

    // ---- pass B: decode -> transform -> encode ------------------
    AiBounceContext ctx;
    ctx.fps = fps;
    ctx.src_w = src_w;
    ctx.src_h = src_h;
    ctx.source_total = src_total;
    ctx.out_total = out_total;

    MediaDecoderPtr decoder = MakeBridgeDecoder();
    if (decoder == nullptr) return fail("no media decoder");
    {
        std::string err;
        if (!decoder->Open(params.source_path, &err)) {
            return fail(err.empty() ? "decoder open failed" : err);
        }
    }
    const double eps = FrameEps(fps);
    if (span_in > 0.0) decoder->Seek(span_in);
    auto dec = std::shared_ptr<IMediaDecoder>(std::move(decoder));
    ctx.next_source = [dec, span_in, span_out, eps](
                          AiBounceFrame* frame,
                          std::string* err) -> bool {
        if (frame == nullptr) {
            if (err) *err = "null frame out";
            return false;
        }
        // Forward-filter to the span (pre-span frames come off the
        // keyframe-backward seek and are skipped here).
        for (;;) {
            if (!dec->NextFrame(&frame->bgra, &frame->w,
                                &frame->h)) {
                return false;
            }
            frame->t = dec->GetPositionSeconds();
            if (frame->t < span_in - eps) continue;
            if (frame->t >= span_out - eps) return false;
            return true;
        }
    };

    const std::filesystem::path target_path =
        Path::FromUtf8(params.output_path);
    std::filesystem::path temp_path =
        XER::Cache::TempPathForTarget(target_path, "xerbnc");
    temp_path += target_path.extension();
    const std::string temp_utf8 = Path::ToUtf8(temp_path);
    {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        ec.clear();
        if (temp_path.has_parent_path()) {
            std::filesystem::create_directories(
                temp_path.parent_path(), ec);
        }
    }

    std::atomic<bool> cancelled{false};
    int enc_w = 0, enc_h = 0;
    auto video_provider = [&](std::size_t i,
                              FFmpeg::VideoFrameInfo* out_frame,
                              std::string* perr) -> bool {
        if (should_cancel != nullptr && should_cancel()) {
            cancelled.store(true);
            if (perr) *perr = "bounce cancelled";
            return false;
        }
        std::vector<std::uint8_t> out;
        int w = 0, h = 0;
        std::string err;
        if (!transform(i, ctx, &out, &w, &h, &err)) {
            if (perr) {
                *perr = err.empty() ? "transform failed" : err;
            }
            return false;
        }
        if (w <= 0 || h <= 0 ||
            out.size() < static_cast<std::size_t>(w) * h * 4u) {
            if (perr) *perr = "transform produced a bad frame";
            return false;
        }
        out_frame->width = w;
        out_frame->height = h;
        enc_w = w;
        enc_h = h;
        out_frame->pixel_format = "bgra";
        out_frame->pts = static_cast<std::int64_t>(i);
        out_frame->best_effort_timestamp =
            static_cast<std::int64_t>(i);
        out_frame->line_sizes = {w * 4, 0, 0, 0};
        out_frame->data = std::move(out);
        if (progress != nullptr) {
            progress(static_cast<float>(i + 1) /
                     static_cast<float>(out_total));
        }
        return true;
    };
    auto audio_provider = [&](std::size_t j,
                              FFmpeg::AudioFrameInfo* out_frame,
                              std::string* perr) -> bool {
        if (j >= audio_chunks.size()) {
            if (perr) *perr = "audio provider over-run";
            return false;
        }
        *out_frame = audio_chunks[j];
        return true;
    };

    FFmpeg::EncodeVideoParams vp;
    vp.output_path = temp_utf8;
    vp.codec_name = params.video_codec;
    vp.width = 0;   // resolved from the first produced frame
    vp.height = 0;
    vp.pixel_format = "bgra";
    {
        // fps double -> rational. Integer-ish rates stay exact;
        // NTSC-ish decimals ride the x1000 approximation the same
        // way the bin's open-in-source does.
        const double rounded = std::round(fps);
        if (fps > 0.0 && std::fabs(fps - rounded) < 1e-3) {
            vp.fps_num = static_cast<int>(rounded);
            vp.fps_den = 1;
        } else if (fps > 0.0) {
            vp.fps_num = static_cast<int>(fps * 1000.0 + 0.5);
            vp.fps_den = 1000;
        }
    }
    vp.bit_rate = params.bit_rate;
    vp.crf = params.crf;

    std::string error;
    bool encoded = false;
    const bool mux = !audio_chunks.empty();
    if (mux) {
        FFmpeg::EncodeAudioParams ap;
        ap.output_path = temp_utf8;
        ap.codec_name = "aac";
        ap.sample_rate = audio_chunks.front().sample_rate;
        ap.channels = audio_chunks.front().channels;
        ap.sample_format = "s16";
        ap.bit_rate = 192000;
        FFmpeg::EncodeAudioVideoParams both;
        both.video = vp;
        both.audio = ap;
        encoded = FFmpeg::EncodeAudioVideoStream(
            both, out_total, video_provider, audio_chunks.size(),
            audio_provider, &error);
    } else {
        encoded = FFmpeg::EncodeVideoFrameStream(
            vp, out_total, video_provider, &error);
    }
    dec->Close();

    if (!encoded) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        if (cancelled.load()) return fail("cancelled");
        return fail(error.empty() ? "bounce encode failed" : error);
    }
    if (should_cancel != nullptr && should_cancel()) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        return fail("cancelled");
    }
    if (!XER::Cache::CommitTempFile(temp_path, target_path, &error)) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        return fail(error.empty() ? "bounce commit failed" : error);
    }

    if (result != nullptr) {
        result->output_path = params.output_path;
        result->source_frames = src_total;
        result->output_frames = out_total;
        result->out_w = enc_w;
        result->out_h = enc_h;
        result->fps = fps;
        result->duration_seconds =
            fps > 0.0 ? static_cast<double>(out_total) / fps : 0.0;
        result->has_audio = mux;
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
