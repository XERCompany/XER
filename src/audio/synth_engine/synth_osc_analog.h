#pragma once

namespace XER::Engine::Audio::Synth {

class SynthOscAnalog {
public:
    void SetFrequency(float hz);
    float Process(float sample_rate);

private:
    float frequency_hz_ = 440.0f;
    float phase_ = 0.0f;
};

}  // namespace XER::Engine::Audio::Synth
