#include "midi_clock_generator.h"

namespace XER::Engine::Audio::MIDI {

void MidiClockGenerator::SetTempoBpm(double bpm) {
    if (bpm > 0.0) {
        tempo_bpm_ = bpm;
    }
}

uint32_t MidiClockGenerator::AdvanceSamples(uint32_t samples, uint32_t sample_rate) {
    if (sample_rate == 0) {
        return 0;
    }

    const double pulses_per_second = (tempo_bpm_ / 60.0) * 24.0;
    pulse_accumulator_ += (static_cast<double>(samples) / sample_rate) * pulses_per_second;

    const uint32_t pulses = static_cast<uint32_t>(pulse_accumulator_);
    pulse_accumulator_ -= pulses;
    return pulses;
}

}  // namespace XER::Engine::Audio::MIDI
