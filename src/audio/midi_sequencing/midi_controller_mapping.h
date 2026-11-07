#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "async_io/sync_primitives/mutex_wrapper.h"
#include "midi_parser.h"

namespace XER::Engine::Audio::MIDI {

struct MidiControllerBinding {
    uint32_t param_id = 0;
    float min_value = 0.0f;
    float max_value = 1.0f;
    bool invert = false;
    int channel = -1;
    std::string label;
};

class MidiControllerMapping {
public:
    void Bind(
        uint8_t cc,
        uint32_t param_id,
        float min_value = 0.0f,
        float max_value = 1.0f,
        bool invert = false,
        int channel = -1,
        const std::string& label = {});
    bool Resolve(uint8_t cc, uint32_t& param_id) const;
    bool ResolveBinding(uint8_t cc, MidiControllerBinding* binding) const;
    bool MapEvent(const MidiEvent& event, uint32_t* param_id, float* mapped_value) const;
    void Clear();

private:
    mutable XER::AsyncIO::IO::Sync::MutexWrapper mutex_{"midi_cc_map"};
    std::unordered_map<uint8_t, MidiControllerBinding> map_;
};

}  // namespace XER::Engine::Audio::MIDI
