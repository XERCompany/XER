#include "ai/audio_pcm.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include "media_decoder.h"
#include "ffmpeg_engine_bridge.h"

namespace XER::VideoEditor::Ai {
namespace {

// One AudioFrameInfo -> float mono at the stream's own rate.
// FFmpeg formats seen from the tap: u8(u8p) s16(p) s32(p) flt(p) dbl(p).
template <typename T>
void ConvertInterleaved(const std::uint8_t* data, int frames, int channels,
                        float scale, std::vector<float>* out) {
    const T* s = reinterpret_cast<const T*>(data);
    for (int i = 0; i < frames; ++i) {
        float acc = 0.0f;
        for (int c = 0; c < channels; ++c) {
            acc += static_cast<float>(s[i * channels + c]) * scale;
        }
        out->push_back(acc / static_cast<float>(channels));
    }
}

template <typename T>
void ConvertPlanar(const std::uint8_t* data,
                   const std::vector<int>& plane_sizes, int frames,
                   int channels, float scale, std::vector<float>* out) {
    const std::size_t base = out->size();
    out->resize(base + static_cast<std::size_t>(frames), 0.0f);
    int offset = 0;
    for (int c = 0; c < channels; ++c) {
        const T* plane = reinterpret_cast<const T*>(data + offset);
        offset += (c < static_cast<int>(plane_sizes.size()))
                      ? plane_sizes[c]
                      : frames * static_cast<int>(sizeof(T));
        for (int i = 0; i < frames; ++i) {
            (*out)[base + i] += static_cast<float>(plane[i]) * scale /
                                static_cast<float>(channels);
        }
    }
}

// Bytes per sample for the FFmpeg formats the tap emits, or 0 for
// anything else (the caller then reports "unsupported format").
int SampleBytes(const std::string& fmt) {
    if (fmt == "flt" || fmt == "fltp") return 4;
    if (fmt == "s16" || fmt == "s16p") return 2;
    if (fmt == "s32" || fmt == "s32p") return 4;
    if (fmt == "u8" || fmt == "u8p") return 1;
    if (fmt == "dbl" || fmt == "dblp") return 8;
    return 0;
}

// Payload bounds check — same strictness as the media catalog's
// ConsumeAudioBlock: planar needs one declared plane per channel,
// each holding at least frames*bps bytes with every declared plane
// inside data (ConvertPlanar uses the reported plane_sizes as its
// step, so an oversized entry must be backed by real bytes too);
// interleaved needs count*channels*bps bytes.
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
bool AppendMono(const XER::Bridge::FFmpeg::AudioFrameInfo& f,
                std::vector<float>* out, std::string* error) {
    const int frames = f.sample_count;
    const int channels = f.channels;
    if (frames <= 0 || f.data.empty()) return true;
    if (channels <= 0) {
        *error = "malformed audio frame";
        return false;
    }
    const std::string& fmt = f.sample_format;
    const int bps = SampleBytes(fmt);
    if (bps == 0) {
        *error = "unsupported sample format: " + fmt;
        return false;
    }
    if (!FramePayloadValid(f, frames, channels, bps, error)) {
        return false;
    }
    if (fmt == "flt" || fmt == "fltp") {
        if (f.planar) {
            ConvertPlanar<float>(f.data.data(), f.plane_sizes, frames,
                                 channels, 1.0f, out);
        } else {
            ConvertInterleaved<float>(f.data.data(), frames, channels,
                                      1.0f, out);
        }
    } else if (fmt == "s16" || fmt == "s16p") {
        if (f.planar) {
            ConvertPlanar<std::int16_t>(f.data.data(), f.plane_sizes,
                                        frames, channels, 1.0f / 32768.0f,
                                        out);
        } else {
            ConvertInterleaved<std::int16_t>(f.data.data(), frames,
                                             channels, 1.0f / 32768.0f,
                                             out);
        }
    } else if (fmt == "s32" || fmt == "s32p") {
        const float k = 1.0f / 2147483648.0f;
        if (f.planar) {
            ConvertPlanar<std::int32_t>(f.data.data(), f.plane_sizes,
                                        frames, channels, k, out);
        } else {
            ConvertInterleaved<std::int32_t>(f.data.data(), frames,
                                             channels, k, out);
        }
    } else if (fmt == "u8" || fmt == "u8p") {
        // u8 is unsigned-biased; normalize around 128 first.
        const std::size_t base = out->size();
        if (f.planar) {
            ConvertPlanar<std::uint8_t>(f.data.data(), f.plane_sizes,
                                        frames, channels, 1.0f / 128.0f,
                                        out);
        } else {
            ConvertInterleaved<std::uint8_t>(f.data.data(), frames,
                                             channels, 1.0f / 128.0f,
                                             out);
        }
        for (std::size_t i = base; i < out->size(); ++i) {
            (*out)[i] -= 1.0f;
        }
    } else if (fmt == "dbl" || fmt == "dblp") {
        if (f.planar) {
            ConvertPlanar<double>(f.data.data(), f.plane_sizes, frames,
                                  channels, 1.0f, out);
        } else {
            ConvertInterleaved<double>(f.data.data(), frames, channels,
                                       1.0f, out);
        }
    }
    return true;
}

void ResampleLinear(const std::vector<float>& in, int src_rate,
                    int dst_rate, std::vector<float>* out) {
    if (src_rate <= 0 || dst_rate <= 0 || in.empty()) return;
    if (src_rate == dst_rate) {
        *out = in;
        return;
    }
    const double ratio =
        static_cast<double>(src_rate) / static_cast<double>(dst_rate);
    const std::size_t out_n =
        static_cast<std::size_t>(in.size() / ratio);
    out->resize(out_n);
    for (std::size_t i = 0; i < out_n; ++i) {
        const double src = i * ratio;
        const std::size_t a = static_cast<std::size_t>(src);
        const std::size_t b = std::min(a + 1, in.size() - 1);
        const float frac = static_cast<float>(src - a);
        (*out)[i] = in[a] * (1.0f - frac) + in[b] * frac;
    }
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
    const auto drain = [&]() -> bool {
        while (decoder->ReadAudioFrames(&frames, 64)) {
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
                if (!AppendMono(f, &mono, &conv_error)) {
                    return false;
                }
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
    ResampleLinear(mono, src_rate,
                   target_rate > 0 ? target_rate : src_rate, &resampled);
    out->samples = std::move(resampled);
    out->sample_rate = target_rate > 0 ? target_rate : src_rate;
    out->duration_seconds =
        static_cast<double>(out->samples.size()) / out->sample_rate;
    return true;
}

}  // namespace XER::VideoEditor::Ai
