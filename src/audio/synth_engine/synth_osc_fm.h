#pragma once

namespace XER::Engine::Audio::Synth {

class SynthOscFm {
public:
    void SetCarrier(float hz);
    void SetModulator(float hz);
    void SetIndex(float idx);
    float Process(float sample_rate);

private:
    float carrier_hz_ = 220.0f;
    float mod_hz_ = 110.0f;
    float index_ = 1.0f;
    float carrier_phase_ = 0.0f;
    float mod_phase_ = 0.0f;
};

}  // namespace XER::Engine::Audio::Synth
