#pragma once

#include <cstdint>

#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Audio::MIDI {

class MidiClockGenerator {
public:
    void SetTempoBpm(double bpm);
    uint32_t AdvanceSamples(uint32_t samples, uint32_t sample_rate);

private:
    double tempo_bpm_ = 120.0;
    double pulse_accumulator_ = 0.0;
    XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Audio::MIDI
