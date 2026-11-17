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

// Appends converted mono samples; false on an unsupported format so the
// caller can surface a real error instead of silence.
bool AppendMono(const XER::Bridge::FFmpeg::AudioFrameInfo& f,
                std::vector<float>* out) {
    const int frames = f.sample_count;
    const int channels = std::max(1, f.channels);
    if (frames <= 0 || f.data.empty()) return true;
    const std::string& fmt = f.sample_format;
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
    } else {
        return false;
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
    std::vector<std::uint8_t> bgra;
    int w = 0, h = 0;
    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> frames;
    std::string conv_error;
    const double cap = max_seconds > 0.0 ? max_seconds : 1.0e9;

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
        while (decoder->ReadAudioFrames(&frames, 64)) {
            for (const auto& f : frames) {
                if (f.sample_rate <= 0) continue;
                if (src_rate == 0) src_rate = f.sample_rate;
                if (!AppendMono(f, &mono)) {
                    conv_error = "unsupported sample format: " +
                                 f.sample_format;
                }
            }
        }
    }
    // Final drain after the pump loop.
    while (decoder->ReadAudioFrames(&frames, 64)) {
        for (const auto& f : frames) {
            if (src_rate == 0) src_rate = f.sample_rate;
            AppendMono(f, &mono);
        }
    }
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
