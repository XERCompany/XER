#include "synth_filter_ladder.h"

#include <algorithm>

namespace XER::Audio::Synth {

void SynthFilterLadder::SetCutoff(float normalized) {
    cutoff_ = std::clamp(normalized, 0.001f, 0.99f);
}

void SynthFilterLadder::SetResonance(float resonance) {
    resonance_ = std::clamp(resonance, 0.0f, 1.0f);
}

float SynthFilterLadder::Process(float sample) {
    const float in = sample - resonance_ * z4_;
    z1_ += cutoff_ * (in - z1_);
    z2_ += cutoff_ * (z1_ - z2_);
    z3_ += cutoff_ * (z2_ - z3_);
    z4_ += cutoff_ * (z3_ - z4_);
    return z4_;
}

}  // namespace XER::Audio::Synth
