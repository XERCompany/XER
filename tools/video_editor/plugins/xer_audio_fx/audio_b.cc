#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float param(const float* p, int32_t n, int32_t i, float d) {
    return (p && i < n) ? p[i] : d;
}

bool valid(const XER_AudioBuffer* b) {
    return b && b->samples && b->frames > 0 && b->channels > 0;
}

}  // namespace

int bitcrusher(XER_AudioBuffer* b, const float* params, int32_t n) {
    if (!valid(b)) return 0;
    const int bits = std::clamp(static_cast<int>(param(params, n, 0, 8.f)), 1, 16);
    const int factor = std::clamp(static_cast<int>(param(params, n, 1, 4.f)), 1, 64);
    const float scale = std::ldexp(1.f, bits - 1);
    std::vector<float> hold(static_cast<size_t>(b->channels), 0.f);
    for (int32_t i = 0; i < b->frames; ++i) {
        float* s = b->samples + static_cast<size_t>(i) * b->channels;
        if (i % factor == 0)
            for (int32_t c = 0; c < b->channels; ++c)
                hold[c] = std::round(s[c] * scale) / scale;
        for (int32_t c = 0; c < b->channels; ++c) s[c] = hold[c];
    }
    return 0;
}

int onepole_lp(XER_AudioBuffer* b, const float* params, int32_t n) {
    if (!valid(b) || b->sample_rate <= 0) return 0;
    const float cutoff = std::clamp(param(params, n, 0, 4000.f), 100.f, 18000.f);
    const float a = std::min(1.f - std::exp(-2.f * kPi * cutoff / b->sample_rate), 1.f);
    std::vector<float> y(static_cast<size_t>(b->channels), 0.f);
    for (int32_t i = 0; i < b->frames; ++i) {
        float* s = b->samples + static_cast<size_t>(i) * b->channels;
        for (int32_t c = 0; c < b->channels; ++c) {
            y[c] += a * (s[c] - y[c]);
            s[c] = y[c];
        }
    }
    return 0;
}

int soft_clip(XER_AudioBuffer* b, const float* params, int32_t n) {
    if (!valid(b)) return 0;
    const float drive = std::clamp(param(params, n, 0, 4.f), 1.f, 20.f);
    const float inv_norm = 1.f / std::tanh(drive);
    const int64_t total = static_cast<int64_t>(b->frames) * b->channels;
    for (int64_t i = 0; i < total; ++i)
        b->samples[i] = std::tanh(b->samples[i] * drive) * inv_norm;
    return 0;
}

}  // namespace fx
