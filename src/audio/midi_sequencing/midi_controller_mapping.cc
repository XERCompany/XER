#include "midi_controller_mapping.h"

#include <algorithm>

namespace XER::Engine::Audio::MIDI {

void MidiControllerMapping::Bind(
    uint8_t cc,
    uint32_t param_id,
    float min_value,
    float max_value,
    bool invert,
    int channel,
    const std::string& label) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    map_[cc] = MidiControllerBinding{param_id, min_value, max_value, invert, channel, label};
}

bool MidiControllerMapping::Resolve(uint8_t cc, uint32_t& param_id) const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    const auto it = map_.find(cc);
    if (it == map_.end()) {
        return false;
    }
    param_id = it->second.param_id;
    return true;
}

bool MidiControllerMapping::ResolveBinding(uint8_t cc, MidiControllerBinding* binding) const {
    if (binding == nullptr) {
        return false;
    }
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    const auto it = map_.find(cc);
    if (it == map_.end()) {
        return false;
    }
    *binding = it->second;
    return true;
}

bool MidiControllerMapping::MapEvent(const MidiEvent& event, uint32_t* param_id, float* mapped_value) const {
    if (param_id == nullptr || mapped_value == nullptr || event.type != MidiMessageType::kControlChange) {
        return false;
    }

    MidiControllerBinding binding;
    if (!ResolveBinding(event.data1, &binding)) {
        return false;
    }
    if (binding.channel >= 0 && binding.channel != static_cast<int>(event.channel)) {
        return false;
    }

    const float normalized = static_cast<float>(event.data2) / 127.0f;
    const float value = binding.invert ? (1.0f - normalized) : normalized;
    *param_id = binding.param_id;
    *mapped_value = binding.min_value + ((binding.max_value - binding.min_value) * value);
    return true;
}

void MidiControllerMapping::Clear() {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    map_.clear();
}

}  // namespace XER::Engine::Audio::MIDI
