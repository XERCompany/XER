#pragma once

namespace XER::Audio::Synth {

class SynthEnvAdsr {
public:
    void Set(float attack_s, float decay_s, float sustain, float release_s);
    void NoteOn();
    void NoteOff();
    float Process(float sample_rate);

private:
    enum class State { Idle, Attack, Decay, Sustain, Release };
    State state_ = State::Idle;
    float attack_s_ = 0.01f;
    float decay_s_ = 0.1f;
    float sustain_ = 0.7f;
    float release_s_ = 0.2f;
    float value_ = 0.0f;
};

}  // namespace XER::Audio::Synth
