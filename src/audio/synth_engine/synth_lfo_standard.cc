#include "synth_lfo_standard.h"

#include <algorithm>
#include <cmath>

namespace XER::Audio::Synth {

void SynthLfoStandard::SetRateHz(float rate_hz) {
    rate_hz_ = std::max(rate_hz, 0.01f);
}

float SynthLfoStandard::Process(float sample_rate) {
    if (sample_rate <= 0.0f) {
        return 0.0f;
    }
    const float out = std::sin(phase_);
    phase_ += 6.28318530718f * rate_hz_ / sample_rate;
    if (phase_ > 6.28318530718f) {
        phase_ -= 6.28318530718f;
    }
    return out;
}

}  // namespace XER::Audio::Synth
