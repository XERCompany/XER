#include "ai/audio_pcm.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "audio/audio_core/audio_bit_depth_converter.h"
#include "audio/audio_core/audio_sample_rate_converter.h"
#include "media_decoder.h"
#include "ffmpeg_engine_bridge.h"

namespace XER::VideoEditor::Ai {
namespace {

using XER::Audio::Core::AudioBitDepth;
using XER::Audio::Core::AudioBitDepthConverter;
using XER::Audio::Core::AudioSampleRateConverter;
using XER::Audio::Core::ResampleQuality;

// Map the FFmpeg sample-format names the tap emits onto the audio-core
// bit-depth enum (u8(u8p) s16(p) s32(p) flt(p) dbl(p)); false for
// anything else (the caller then reports "unsupported format").
bool FormatToBitDepth(const std::string& fmt, AudioBitDepth* out) {
    if (fmt == "u8"  || fmt == "u8p")  { *out = AudioBitDepth::BITS_8;   return true; }
    if (fmt == "s16" || fmt == "s16p") { *out = AudioBitDepth::BITS_16;  return true; }
    if (fmt == "s32" || fmt == "s32p") { *out = AudioBitDepth::BITS_32;  return true; }
    if (fmt == "flt" || fmt == "fltp") { *out = AudioBitDepth::FLOAT_32; return true; }
    if (fmt == "dbl" || fmt == "dblp") { *out = AudioBitDepth::FLOAT_64; return true; }
    return false;
}

// Payload bounds check — same strictness as the media catalog's
// ConsumeAudioBlock: planar needs one declared plane per channel,
// each holding at least frames*bps bytes with every declared plane
// inside data (AppendMono's planar path uses the reported plane_sizes
// as its step, so an oversized entry must be backed by real bytes
// too); interleaved needs count*channels*bps bytes.
bool FramePayloadValid(const XER::Bridge::FFmpeg::AudioFrameInfo& f,
                       int frames, int channels, int bps,
                       std::string* error) {
    const std::size_t need_per_ch =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(bps);
    if (f.planar) {
        if (f.plane_sizes.size() < static_cast<std::size_t>(channels)) {
            *error = "planar audio frame missing plane sizes";
            return false;
        }
        std::size_t offset = 0;
        for (int c = 0; c < channels; ++c) {
            const int ps = f.plane_sizes[static_cast<std::size_t>(c)];
            if (ps < 0 ||
                static_cast<std::size_t>(ps) < need_per_ch) {
                *error = "planar audio frame plane truncated";
                return false;
            }
            offset += static_cast<std::size_t>(ps);
            if (offset > f.data.size()) {
                *error = "planar audio frame payload truncated";
                return false;
            }
        }
        return true;
    }
    const std::size_t need =
        need_per_ch * static_cast<std::size_t>(channels);
    if (f.data.size() < need) {
        *error = "interleaved audio frame payload truncated";
        return false;
    }
    return true;
}

// Appends converted mono samples; false on an unsupported format or a
// malformed payload so the caller can surface a real error instead of
// silence. The payload is bounds-checked before any sample is read —
// decoder-reported sizes are untrusted (see media_catalog.cc's
// ConsumeAudioBlock for the twin validation).
//
// Sample-format conversion is delegated to
// AudioBitDepthConverter (u8 bias, int32/double handling identical to
// the retired per-format templates); the channel mixdown stays here —
// it is the editor's mono contract, not a format concern.
bool AppendMono(const XER::Bridge::FFmpeg::AudioFrameInfo& f,
                AudioBitDepthConverter* converter,
                std::vector<float>* out, std::string* error) {
    const int frames = f.sample_count;
    const int channels = f.channels;
    if (frames <= 0 || f.data.empty()) return true;
    if (channels <= 0) {
        *error = "malformed audio frame";
        return false;
    }
    AudioBitDepth depth;
    if (!FormatToBitDepth(f.sample_format, &depth)) {
        *error = "unsupported sample format: " + f.sample_format;
        return false;
    }
    const int bps = static_cast<int>(
        AudioBitDepthConverter::GetBytesPerSample(depth));
    if (!FramePayloadValid(f, frames, channels, bps, error)) {
        return false;
    }
    const float inv_ch = 1.0f / static_cast<float>(channels);
    if (f.planar) {
        const std::size_t base = out->size();
        out->resize(base + static_cast<std::size_t>(frames), 0.0f);
        std::vector<float> plane_f(static_cast<std::size_t>(frames));
        std::size_t offset = 0;
        for (int c = 0; c < channels; ++c) {
            const std::uint8_t* plane = f.data.data() + offset;
            offset += static_cast<std::size_t>(
                f.plane_sizes[static_cast<std::size_t>(c)]);
            if (!converter->ConvertBuffer(plane, depth, plane_f.data(),
                                          AudioBitDepth::FLOAT_32,
                                          static_cast<std::size_t>(frames))) {
                *error = "sample conversion failed: " + f.sample_format;
                return false;
            }
            for (int i = 0; i < frames; ++i) {
                (*out)[base + i] += plane_f[static_cast<std::size_t>(i)] * inv_ch;
            }
        }
    } else {
        const std::size_t total =
            static_cast<std::size_t>(frames) * channels;
        std::vector<float> block(total);
        if (!converter->ConvertBuffer(f.data.data(), depth, block.data(),
                                      AudioBitDepth::FLOAT_32, total)) {
            *error = "sample conversion failed: " + f.sample_format;
            return false;
        }
        for (int i = 0; i < frames; ++i) {
            float acc = 0.0f;
            for (int c = 0; c < channels; ++c) {
                acc += block[static_cast<std::size_t>(i) * channels + c];
            }
            out->push_back(acc * inv_ch);
        }
    }
    return true;
}

// One-shot mono resample through the audio-core converter. FAST quality
// selects its linear-interpolation branch — the same math as the
// hand-rolled resampler this replaces. (BEST would switch to windowed
// sinc: better anti-aliasing but an intentional quality change, and its
// tail length differs — kept for a follow-up if whisper STT accuracy
// ever needs it.)
bool ResampleMono(const std::vector<float>& in, int src_rate, int dst_rate,
                  std::vector<float>* out, std::string* error) {
    if (src_rate <= 0 || dst_rate <= 0 || in.empty()) {
        out->clear();
        return src_rate > 0 && dst_rate > 0;
    }
    if (src_rate == dst_rate) {
        *out = in;
        return true;
    }
    AudioSampleRateConverter converter(ResampleQuality::FAST);
    if (!converter.Initialize(src_rate, dst_rate)) {
        *error = "sample-rate converter init failed";
        return false;
    }
    // Convert() sizes its own output; allocate the documented upper
    // bound (ceil(n*ratio) plus a guard sample) then shrink to fit.
    const std::size_t cap_n = static_cast<std::size_t>(std::ceil(
        static_cast<double>(in.size()) * converter.GetRatio())) + 1;
    out->assign(cap_n, 0.0f);
    std::size_t produced = 0;
    if (!converter.Convert(in.data(), in.size(), out->data(), produced)) {
        *error = "sample-rate conversion failed";
        return false;
    }
    out->resize(produced);
    return true;
}

}  // namespace

bool ExtractMonoPcm(const std::string& utf8_path, int target_rate,
                    double max_seconds, Pcm* out, std::string* error_out) {
    if (out == nullptr) return false;
    out->samples.clear();
    out->sample_rate = 0;
    out->duration_seconds = 0.0;

    MediaDecoderPtr decoder = MakeBridgeDecoder();
    if (decoder == nullptr) {
        if (error_out) *error_out = "no decoder available";
        return false;
    }
    decoder->EnableAudioTap(true);
    std::string open_error;
    if (!decoder->Open(utf8_path, &open_error)) {
        if (error_out) {
            *error_out = open_error.empty() ? "decoder open failed"
                                            : open_error;
        }
        return false;
    }
    if (!decoder->HasAudio()) {
        decoder->Close();
        if (error_out) *error_out = "media has no audio stream";
        return false;
    }

    std::vector<float> mono;
    int src_rate = 0;
    int src_channels = 0;
    std::vector<std::uint8_t> bgra;
    int w = 0, h = 0;
    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> frames;
    std::string conv_error;
    const double cap = max_seconds > 0.0 ? max_seconds : 1.0e9;

    // Drains the tap queue into `mono`. One rate/channel layout per
    // file — a mid-stream change is fatal (same rule as the mixer's
    // ConsumeAudioBlock): silently concatenating a different rate
    // would produce wrong PCM/duration for downstream consumers.
    // Returns false once a conversion error is latched.
    //
    // ReadAudioFrames APPENDS decoded blocks to `frames` — callers
    // clear first — and returns false only when no decode codec was
    // opened or a decode error latched (the former still reaches
    // AudioDrained(), documented OK; the latter spins to the pump
    // guard). Exactly one read per pump pass — the same pattern the
    // mixer (audio_mixer.cc) and peaks builder (media_catalog.cc)
    // use. Looping the read here was wrong twice over: `frames` was
    // never cleared so every pass re-appended all accumulated blocks
    // into `mono` (quadratic growth -> bad_alloc on multi-second
    // audio), and a true return on an empty drain spun the while
    // forever.
    AudioBitDepthConverter bit_depth;
    const auto drain = [&]() -> bool {
        frames.clear();
        decoder->ReadAudioFrames(&frames, 64);
        for (const auto& f : frames) {
            if (f.sample_rate <= 0) continue;
            if (src_rate == 0) {
                src_rate = f.sample_rate;
                src_channels = f.channels;
            } else if (f.sample_rate != src_rate ||
                       f.channels != src_channels) {
                conv_error =
                    "audio sample rate/channels changed "
                    "mid-stream";
                return false;
            }
            if (!AppendMono(f, &bit_depth, &mono, &conv_error)) {
                return false;
            }
        }
        return true;
    };

    // Contract: pump packets via NextFrame (returns false at EOF /
    // on audio-only files but still pumps a bounded batch), drain the
    // tap queue in between, until AudioDrained().
    int guard = 0;
    while (!decoder->AudioDrained() && guard++ < 100000) {
        if (src_rate > 0 &&
            static_cast<double>(mono.size()) / src_rate >= cap) {
            break;
        }
        decoder->NextFrame(&bgra, &w, &h);
        if (!drain()) break;
    }
    // Final drain after the pump loop (skipped once an error latched).
    if (conv_error.empty()) drain();
    decoder->Close();

    if (!conv_error.empty()) {
        if (error_out) *error_out = conv_error;
        return false;
    }
    if (mono.empty() || src_rate <= 0) {
        if (error_out) *error_out = "decoded audio is empty";
        return false;
    }
    const std::size_t cap_samples = static_cast<std::size_t>(
        cap * static_cast<double>(src_rate));
    if (mono.size() > cap_samples) mono.resize(cap_samples);

    std::vector<float> resampled;
    std::string resample_error;
    if (!ResampleMono(mono, src_rate,
                      target_rate > 0 ? target_rate : src_rate,
                      &resampled, &resample_error)) {
        if (error_out) *error_out = resample_error;
        return false;
    }
    out->samples = std::move(resampled);
    out->sample_rate = target_rate > 0 ? target_rate : src_rate;
    out->duration_seconds =
        static_cast<double>(out->samples.size()) / out->sample_rate;
    return true;
}

}  // namespace XER::VideoEditor::Ai
