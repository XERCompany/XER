#pragma once

#include <cstdint>
#include <vector>

#include "async_io/log_and_debug/io_dump_helper.h"

namespace XER::Audio::MIDI {

struct MidiSysexMessage {
    uint32_t manufacturer_id = 0;
    bool universal_non_realtime = false;
    bool universal_realtime = false;
    uint8_t device_id = 0x7F;
    uint8_t sub_id1 = 0;
    uint8_t sub_id2 = 0;
    std::vector<uint8_t> payload;
};

class MidiSysexHandler {
public:
    bool IsValidSysex(const std::vector<uint8_t>& msg) const;
    bool Parse(const std::vector<uint8_t>& msg, MidiSysexMessage* out) const;
    std::vector<uint8_t> BuildIdentityRequest(uint8_t device_id = 0x7F) const;
    std::vector<uint8_t> BuildIdentityReply(
        uint8_t device_id,
        uint32_t manufacturer_id,
        uint16_t family_code,
        uint16_t model_number,
        uint32_t version) const;
};

}  // namespace XER::Audio::MIDI
