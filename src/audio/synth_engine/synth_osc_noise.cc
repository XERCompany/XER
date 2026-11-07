#include "synth_osc_noise.h"

namespace XER::Engine::Audio::Synth {

void SynthOscNoise::SetSeed(unsigned int seed) {
    state_ = seed == 0 ? 0x12345678u : seed;
}

float SynthOscNoise::Process() {
    state_ = 1664525u * state_ + 1013904223u;
    const float normalized = static_cast<float>((state_ >> 8) & 0x00FFFFFFu) / 8388607.5f - 1.0f;
    return normalized;
}

}  // namespace XER::Engine::Audio::Synth
