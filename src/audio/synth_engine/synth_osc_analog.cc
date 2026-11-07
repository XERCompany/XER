#include "synth_osc_analog.h"

#include <algorithm>

namespace XER::Engine::Audio::Synth {

void SynthOscAnalog::SetFrequency(float hz) {
    frequency_hz_ = std::max(hz, 1.0f);
}

float SynthOscAnalog::Process(float sample_rate) {
    if (sample_rate <= 0.0f) {
        return 0.0f;
    }
    const float out = 2.0f * (phase_ - 0.5f);
    phase_ += frequency_hz_ / sample_rate;
    if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
    }
    return out;
}

}  // namespace XER::Engine::Audio::Synth
