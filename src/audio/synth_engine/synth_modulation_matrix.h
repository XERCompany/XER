#pragma once

#include <cstdint>
#include <limits>
 
#include <absl/container/flat_hash_map.h> 

namespace XER::Engine::Audio::Synth {

class SynthModulationMatrix {
public:
    void SetAmount(uint32_t source, uint32_t destination, float amount);
    float GetAmount(uint32_t source, uint32_t destination) const;

    void SetSourceValue(uint32_t source, float value);
    float GetSourceValue(uint32_t source) const;

    void ConfigureDestination(uint32_t destination, float base_value, float minimum_value, float maximum_value);
    bool Apply(uint32_t destination, float* value_out);
    float GetDestinationValue(uint32_t destination) const;
    void Reset();

private:
    struct DestinationState {
        float base_value = 0.0f;
        float minimum_value = -std::numeric_limits<float>::max();
        float maximum_value = std::numeric_limits<float>::max();
        float current_value = 0.0f;
    };

    static uint64_t MakeRouteKey(uint32_t source, uint32_t destination);

private: 
    absl::flat_hash_map<uint64_t, float> routes_; 
    absl::flat_hash_map<uint32_t, float> source_values_;
    absl::flat_hash_map<uint32_t, DestinationState> destination_states_;
};

}  // namespace XER::Engine::Audio::Synth
