#include "synth_modulation_matrix.h"

#include <algorithm>

namespace XER::Engine::Audio::Synth {

uint64_t SynthModulationMatrix::MakeRouteKey(uint32_t source, uint32_t destination) {
    return (static_cast<uint64_t>(source) << 32) | destination;
}

void SynthModulationMatrix::SetAmount(uint32_t source, uint32_t destination, float amount) {
    const uint64_t key = MakeRouteKey(source, destination);
    if (amount == 0.0f) {
        routes_.erase(key);
        return;
    }
    routes_[key] = amount;
}

float SynthModulationMatrix::GetAmount(uint32_t source, uint32_t destination) const {
    const uint64_t key = MakeRouteKey(source, destination);
    const auto it = routes_.find(key);
    if (it == routes_.end()) {
        return 0.0f;
    }
    return it->second;
}

void SynthModulationMatrix::SetSourceValue(uint32_t source, float value) {
    source_values_[source] = value;
}

float SynthModulationMatrix::GetSourceValue(uint32_t source) const {
    const auto it = source_values_.find(source);
    if (it == source_values_.end()) {
        return 0.0f;
    }
    return it->second;
}

void SynthModulationMatrix::ConfigureDestination(
    uint32_t destination,
    float base_value,
    float minimum_value,
    float maximum_value) {
    DestinationState& state = destination_states_[destination];
    state.base_value = base_value;
    state.minimum_value = std::min(minimum_value, maximum_value);
    state.maximum_value = std::max(minimum_value, maximum_value);
    state.current_value = std::clamp(base_value, state.minimum_value, state.maximum_value);
}

bool SynthModulationMatrix::Apply(uint32_t destination, float* value_out) {
    if (value_out == nullptr) {
        return false;
    }

    DestinationState& state = destination_states_[destination];
    float value = state.base_value;
    for (const auto& route : routes_) {
        const uint32_t route_destination = static_cast<uint32_t>(route.first & 0xffffffffu);
        if (route_destination != destination) {
            continue;
        }

        const uint32_t route_source = static_cast<uint32_t>(route.first >> 32);
        const auto source_it = source_values_.find(route_source);
        if (source_it == source_values_.end()) {
            continue;
        }

        value += source_it->second * route.second;
    }

    state.current_value = std::clamp(value, state.minimum_value, state.maximum_value);
    *value_out = state.current_value;
    return true;
}

float SynthModulationMatrix::GetDestinationValue(uint32_t destination) const {
    const auto it = destination_states_.find(destination);
    if (it == destination_states_.end()) {
        return 0.0f;
    }
    return it->second.current_value;
}

void SynthModulationMatrix::Reset() {
    source_values_.clear();
    destination_states_.clear();
}

}  // namespace XER::Engine::Audio::Synth
