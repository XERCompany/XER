#include "midi_pattern_sequencer.h"

#include <algorithm>

namespace XER::Audio::MIDI {

void MidiPatternSequencer::SetPattern(const std::vector<MidiEvent>& pattern) {
    pattern_ = pattern;
    std::sort(pattern_.begin(), pattern_.end(), [](const MidiEvent& lhs, const MidiEvent& rhs) {
        return lhs.tick < rhs.tick;
    });
}

void MidiPatternSequencer::SetLoopLength(uint32_t loop_length_ticks) {
    loop_length_ticks_ = loop_length_ticks;
}

std::vector<MidiEvent> MidiPatternSequencer::CollectRange(uint32_t start_tick, uint32_t end_tick) const {
    std::vector<MidiEvent> out;
    if (start_tick >= end_tick || pattern_.empty()) {
        return out;
    }

    if (loop_length_ticks_ == 0) {
        for (const auto& event : pattern_) {
            if (event.tick >= start_tick && event.tick < end_tick) {
                out.push_back(event);
            }
        }
        return out;
    }

    const uint32_t first_loop = start_tick / loop_length_ticks_;
    const uint32_t last_loop = (end_tick - 1) / loop_length_ticks_;
    for (uint32_t loop_index = first_loop; loop_index <= last_loop; ++loop_index) {
        const uint64_t loop_offset = static_cast<uint64_t>(loop_index) * static_cast<uint64_t>(loop_length_ticks_);
        for (const auto& event : pattern_) {
            const uint64_t absolute_tick = loop_offset + static_cast<uint64_t>(event.tick % loop_length_ticks_);
            if (absolute_tick < start_tick || absolute_tick >= end_tick) {
                continue;
            }
            MidiEvent scheduled = event;
            scheduled.tick = static_cast<uint32_t>(absolute_tick);
            out.push_back(std::move(scheduled));
        }
    }
    std::sort(out.begin(), out.end(), [](const MidiEvent& lhs, const MidiEvent& rhs) {
        return lhs.tick < rhs.tick;
    });
    return out;
}

}  // namespace XER::Audio::MIDI
