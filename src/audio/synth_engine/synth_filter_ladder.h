#pragma once

namespace XER::Audio::Synth {

class SynthFilterLadder {
public:
    void SetCutoff(float normalized);
    void SetResonance(float resonance);
    float Process(float sample);

private:
    float cutoff_ = 0.2f;
    float resonance_ = 0.1f;
    float z1_ = 0.0f, z2_ = 0.0f, z3_ = 0.0f, z4_ = 0.0f;
};

}  // namespace XER::Audio::Synth
