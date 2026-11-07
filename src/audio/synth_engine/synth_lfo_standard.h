#pragma once

namespace XER::Engine::Audio::Synth {

class SynthLfoStandard {
public:
    void SetRateHz(float rate_hz);
    float Process(float sample_rate);

private:
    float rate_hz_ = 1.0f;
    float phase_ = 0.0f;
};

}  // namespace XER::Engine::Audio::Synth
