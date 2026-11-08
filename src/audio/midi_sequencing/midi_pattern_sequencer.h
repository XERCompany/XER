#pragma once

#include <vector>

#include "async_io/async_task_queue.h"
#include "midi_parser.h"

namespace XER::Audio::MIDI {

class MidiPatternSequencer {
public:
    void SetPattern(const std::vector<MidiEvent>& pattern);
    void SetLoopLength(uint32_t loop_length_ticks);
    std::vector<MidiEvent> CollectRange(uint32_t start_tick, uint32_t end_tick) const;

private:
    std::vector<MidiEvent> pattern_;
    uint32_t loop_length_ticks_ = 0;
};

}  // namespace XER::Audio::MIDI
