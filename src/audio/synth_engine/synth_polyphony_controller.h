#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace XER::Audio::Synth {

class SynthPolyphonyController {
public:
    void SetMaxVoices(uint32_t max_voices);
    int AllocateVoice(uint8_t note, uint8_t* stolen_note = nullptr);
    void ReleaseVoice(uint8_t note);
    bool IsAllocated(uint8_t note) const;
    size_t GetAllocatedVoiceCount() const { return active_notes_.size(); }

private:
    uint32_t max_voices_ = 16;
    std::vector<uint8_t> active_notes_;
};

}  // namespace XER::Audio::Synth
