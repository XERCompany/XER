#include "synth_polyphony_controller.h"

#include <algorithm>

namespace XER::Engine::Audio::Synth {

void SynthPolyphonyController::SetMaxVoices(uint32_t max_voices) {
    max_voices_ = std::max<uint32_t>(1, max_voices);
    while (active_notes_.size() > max_voices_) {
        active_notes_.erase(active_notes_.begin());
    }
}

int SynthPolyphonyController::AllocateVoice(uint8_t note, uint8_t* stolen_note) {
    if (stolen_note != nullptr) {
        *stolen_note = 0xFFu;
    }
    const auto existing = std::find(active_notes_.begin(), active_notes_.end(), note);
    if (existing != active_notes_.end()) {
        active_notes_.erase(existing);
        active_notes_.push_back(note);
        return static_cast<int>(active_notes_.size() - 1);
    }
    if (active_notes_.size() >= max_voices_) {
        if (stolen_note != nullptr && !active_notes_.empty()) {
            *stolen_note = active_notes_.front();
        }
        active_notes_.erase(active_notes_.begin());
    }
    active_notes_.push_back(note);
    return static_cast<int>(active_notes_.size() - 1);
}

void SynthPolyphonyController::ReleaseVoice(uint8_t note) {
    const auto it = std::find(active_notes_.begin(), active_notes_.end(), note);
    if (it != active_notes_.end()) {
        active_notes_.erase(it);
    }
}

bool SynthPolyphonyController::IsAllocated(uint8_t note) const {
    return std::find(active_notes_.begin(), active_notes_.end(), note) != active_notes_.end();
}

}  // namespace XER::Engine::Audio::Synth
