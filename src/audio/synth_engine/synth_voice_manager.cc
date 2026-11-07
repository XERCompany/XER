#include "synth_voice_manager.h"

namespace XER::Engine::Audio::Synth {

void SynthVoiceManager::SetMaxVoices(uint32_t max_voices) {
    polyphony_.SetMaxVoices(max_voices);
}

void SynthVoiceManager::NoteOn(uint8_t note, float freq_hz) {
    uint8_t stolen_note = 0xFFu;
    polyphony_.AllocateVoice(note, &stolen_note);
    if (stolen_note != 0xFFu && stolen_note != note) {
        voices_.erase(stolen_note);
    }
    auto& v = voices_[note];
    v.note = note;
    v.active = true;
    v.osc.SetFrequency(freq_hz);
    v.env.NoteOn();
}

void SynthVoiceManager::NoteOff(uint8_t note) {
    auto it = voices_.find(note);
    if (it != voices_.end()) {
        it->second.active = false;
        it->second.env.NoteOff();
    }
    polyphony_.ReleaseVoice(note);
}

float SynthVoiceManager::RenderSample(float sample_rate) {
    float out = 0.0f;
    for (auto it = voices_.begin(); it != voices_.end();) {
        auto& v = it->second;
        const float env = v.env.Process(sample_rate);
        if (!polyphony_.IsAllocated(v.note) && env <= 1.0e-5f) {
            auto erase_it = it;
            ++it;
            voices_.erase(erase_it);
            continue;
        }
        const float osc = v.osc.Process(sample_rate);
        out += osc * env;
        ++it;
    }
    return out;
}

size_t SynthVoiceManager::GetActiveVoiceCount() const {
    return polyphony_.GetAllocatedVoiceCount();
}

size_t SynthVoiceManager::GetResidentVoiceCount() const {
    return voices_.size();
}

}  // namespace XER::Engine::Audio::Synth
