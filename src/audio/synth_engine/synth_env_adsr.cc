#include "synth_env_adsr.h"

#include <algorithm>

namespace XER::Audio::Synth {

void SynthEnvAdsr::Set(float attack_s, float decay_s, float sustain, float release_s) {
    attack_s_ = std::max(attack_s, 0.0001f);
    decay_s_ = std::max(decay_s, 0.0001f);
    sustain_ = std::clamp(sustain, 0.0f, 1.0f);
    release_s_ = std::max(release_s, 0.0001f);
}

void SynthEnvAdsr::NoteOn() { state_ = State::Attack; }
void SynthEnvAdsr::NoteOff() { state_ = State::Release; }

float SynthEnvAdsr::Process(float sample_rate) {
    if (sample_rate <= 0.0f) {
        return value_;
    }
    switch (state_) {
        case State::Attack:
            value_ += 1.0f / (attack_s_ * sample_rate);
            if (value_ >= 1.0f) { value_ = 1.0f; state_ = State::Decay; }
            break;
        case State::Decay:
            value_ -= (1.0f - sustain_) / (decay_s_ * sample_rate);
            if (value_ <= sustain_) { value_ = sustain_; state_ = State::Sustain; }
            break;
        case State::Sustain:
            value_ = sustain_;
            break;
        case State::Release:
            value_ -= 1.0f / (release_s_ * sample_rate);
            if (value_ <= 0.0f) { value_ = 0.0f; state_ = State::Idle; }
            break;
        case State::Idle:
            value_ = 0.0f;
            break;
    }
    return value_;
}

}  // namespace XER::Audio::Synth
