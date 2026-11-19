#include "media/audio/audio_output_resampler.h"

#include <cstring>

namespace XER::Media {

AudioOutputResampler::AudioOutputResampler(const AudioParameters& from,
                                           const AudioParameters& to)
    : from_(from), to_(to) {}

int AudioOutputResampler::Resample(const std::uint8_t* src, int frames_in,
                                   std::vector<std::uint8_t>* out) {
    if (!NeedsResample()) {
        const int bytes =
            frames_in * from_.channels() * SampleFormatBytes(from_.format);
        out->insert(out->end(), src, src + bytes);
        return frames_in;
    }
    // Linear resample on f32 samples; channel count must match for the
    // simple path (layout conversion handled by the dispatcher).
    const int in_rate = from_.sample_rate;
    const int out_rate = to_.sample_rate;
    const int ch = from_.channels();
    const auto* in = reinterpret_cast<const float*>(src);
    const double step = static_cast<double>(in_rate) / out_rate;
    const int out_frames = static_cast<int>(frames_in / step) + 1;
    out->reserve(out->size() + out_frames * ch * sizeof(float));
    for (int i = 0; i < out_frames; ++i) {
        const double pos = phase_ + i * step;
        const int i0 = static_cast<int>(pos);
        if (i0 >= frames_in - 1) break;
        const double frac = pos - i0;
        for (int c = 0; c < ch; ++c) {
            const float a = in[i0 * ch + c];
            const float b = in[(i0 + 1) * ch + c];
            const float v = a + static_cast<float>(frac) * (b - a);
            const auto* pv = reinterpret_cast<const std::uint8_t*>(&v);
            out->insert(out->end(), pv, pv + sizeof(float));
        }
    }
    phase_ = 0.0;
    return out_frames;
}

}  // namespace XER::Media
