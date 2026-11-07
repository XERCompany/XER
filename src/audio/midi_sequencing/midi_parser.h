#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_dump_helper.h"

namespace XER::Engine::Audio::MIDI {

enum class MidiMessageType {
    kUnknown,
    kNoteOff,
    kNoteOn,
    kPolyphonicAftertouch,
    kControlChange,
    kProgramChange,
    kChannelAftertouch,
    kPitchBend,
    kSongPositionPointer,
    kSongSelect,
    kTimeCodeQuarterFrame,
    kTuneRequest,
    kStart,
    kContinue,
    kStop,
    kTimingClock,
    kActiveSensing,
    kSystemReset,
    kSysEx,
    kSystemCommon,
    kSystemRealtime,
};

struct MidiEvent {
    MidiMessageType type = MidiMessageType::kUnknown;
    uint8_t status = 0;
    uint8_t channel = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint32_t tick = 0;
    bool running_status = false;
    std::vector<uint8_t> message;
};

class MidiParser {
public:
    bool ParseStream(const uint8_t* data, size_t bytes, std::vector<MidiEvent>& out) const;
    static std::string DescribeEventType(MidiMessageType type);
};

}  // namespace XER::Engine::Audio::MIDI
