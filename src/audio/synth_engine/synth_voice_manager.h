#pragma once

#include <cstddef>
#include <cstdint>
 
#include <absl/container/flat_hash_map.h> 

#include "synth_env_adsr.h"
#include "synth_osc_analog.h"
#include "synth_polyphony_controller.h"

namespace XER::Audio::Synth {

struct VoiceState {
    uint8_t note = 0;
    bool active = false;
    SynthOscAnalog osc;
    SynthEnvAdsr env;
};

class SynthVoiceManager {
public:
	void SetMaxVoices(uint32_t max_voices);
    void NoteOn(uint8_t note, float freq_hz);
    void NoteOff(uint8_t note);
    float RenderSample(float sample_rate);
	size_t GetActiveVoiceCount() const;
	size_t GetResidentVoiceCount() const;

private: 
	SynthPolyphonyController polyphony_;
    absl::flat_hash_map<uint8_t, VoiceState> voices_; 
};

}  // namespace XER::Audio::Synth
