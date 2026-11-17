#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

float pget(const float* p, int32_t n, int32_t i, float def, float lo, float hi) {
    const float v = (p != nullptr && i < n) ? p[i] : def;
    return std::clamp(v, lo, hi);
}

bool valid(const XER_AudioBuffer* b) {
    return b != nullptr && b->samples != nullptr && b->frames > 0 &&
           b->channels > 0 && b->sample_rate > 0;
}
}  // namespace

namespace fx {

int echo(XER_AudioBuffer* b, const float* params, int32_t n) {
    if (!valid(b)) return 0;
    const float delay_ms = pget(params, n, 0, 350.f, 1.f, 2000.f);
    const float feedback = pget(params, n, 1, 0.35f, 0.f, 0.95f);
    const float mix = pget(params, n, 2, 0.4f, 0.f, 1.f);
    const int32_t ch = b->channels;
    const int32_t delay = std::max<int32_t>(
        1, static_cast<int32_t>(delay_ms * 0.001f * b->sample_rate + 0.5f));
    std::vector<float> line(static_cast<size_t>(delay) * ch, 0.f);
    const float dry = 1.f - mix;
    int32_t pos = 0;
    for (int32_t f = 0; f < b->frames; ++f) {
        float* frame = b->samples + static_cast<size_t>(f) * ch;
        float* tap = line.data() + static_cast<size_t>(pos) * ch;
        for (int32_t c = 0; c < ch; ++c) {
            const float delayed = tap[c];
            const float in = frame[c];
            frame[c] = in * dry + delayed * mix;
            tap[c] = in + delayed * feedback;
        }
        if (++pos >= delay) pos = 0;
    }
    return 0;
}

int tremolo(XER_AudioBuffer* b, const float* params, int32_t n) {
    if (!valid(b)) return 0;
    const float rate = pget(params, n, 0, 5.f, 0.1f, 20.f);
    const float depth = pget(params, n, 1, 0.6f, 0.f, 1.f);
    const double inc = 2.0 * kPi * rate / b->sample_rate;
    double phase = 0.0;
    const int32_t ch = b->channels;
    for (int32_t f = 0; f < b->frames; ++f) {
        const float gain =
            1.f - depth * 0.5f * static_cast<float>(1.0 + std::sin(phase));
        float* frame = b->samples + static_cast<size_t>(f) * ch;
        for (int32_t c = 0; c < ch; ++c) frame[c] *= gain;
        phase += inc;
        if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
    }
    return 0;
}

int autopan(XER_AudioBuffer* b, const float* params, int32_t n) {
    if (!valid(b) || b->channels < 2) return 0;
    const float rate = pget(params, n, 0, 1.f, 0.1f, 20.f);
    const float depth = pget(params, n, 1, 0.8f, 0.f, 1.f);
    const double inc = 2.0 * kPi * rate / b->sample_rate;
    double phase = 0.0;
    const int32_t ch = b->channels;
    for (int32_t f = 0; f < b->frames; ++f) {
        const float pan =
            0.5f + 0.5f * depth * static_cast<float>(std::sin(phase));
        const float angle = pan * static_cast<float>(kPi * 0.5);
        const float gl = std::cos(angle);
        const float gr = std::sin(angle);
        float* frame = b->samples + static_cast<size_t>(f) * ch;
        frame[0] *= gl;
        frame[1] *= gr;
        phase += inc;
        if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
    }
    return 0;
}

}  // namespace fx
