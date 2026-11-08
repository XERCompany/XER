#include "synth_osc_fm.h"

#include <algorithm>
#include <cmath>

namespace XER::Audio::Synth {

void SynthOscFm::SetCarrier(float hz) { carrier_hz_ = std::max(hz, 1.0f); }
void SynthOscFm::SetModulator(float hz) { mod_hz_ = std::max(hz, 1.0f); }
void SynthOscFm::SetIndex(float idx) { index_ = std::max(idx, 0.0f); }

float SynthOscFm::Process(float sample_rate) {
    if (sample_rate <= 0.0f) {
        return 0.0f;
    }

    const float mod = std::sin(mod_phase_) * index_;
    const float out = std::sin(carrier_phase_ + mod);

    carrier_phase_ += 6.28318530718f * carrier_hz_ / sample_rate;
    mod_phase_ += 6.28318530718f * mod_hz_ / sample_rate;
    if (carrier_phase_ > 6.28318530718f) carrier_phase_ -= 6.28318530718f;
    if (mod_phase_ > 6.28318530718f) mod_phase_ -= 6.28318530718f;
    return out;
}

}  // namespace XER::Audio::Synth
