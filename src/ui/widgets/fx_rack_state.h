#ifndef XER_UI_WIDGETS_FX_RACK_STATE_H
#define XER_UI_WIDGETS_FX_RACK_STATE_H

#include <array>
#include <string>
#include <vector>

#include "audio/effects_rack/custom_effect_struct.h"

namespace XER::UI::Widgets {

// Editable parameter description for one custom-effect node field.
struct FxParamSpec {
    std::string name;
    std::string label;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float default_value = 0.0f;
};

struct FxEqBandState {
    float frequency_hz = 1000.0f;
    float q = 1.0f;
    float gain_db = 0.0f;
};

struct FxNodeState {
    XER::Engine::Audio::FX::CustomEffectAlgorithm algorithm =
        XER::Engine::Audio::FX::CustomEffectAlgorithm::kUnknown;
    bool enabled = true;
    // Wet/dry amount for algorithms that support a node-level mix curve.
    float wet_mix = 1.0f;
    std::vector<XER::Engine::Audio::FX::CustomEffectParameter> params;
};

// Shared, UI-free model of the whole FX rack: graphic EQ, bass boost,
// reverb and the user-assembled effect chain. Both the player widget and
// the dedicated FX window edit this state; FxRackWidget compiles it into a
// CustomEffectPackage and applies it live.
class FxRackState {
public:
    FxRackState();

    bool enabled = false;
    std::array<FxEqBandState, 5> eq_bands;
    float bass_boost_db = 0.0f;
    float reverb_mix = 0.0f;
    float reverb_room_size = 0.6f;
    float reverb_damping = 0.3f;
    std::vector<FxNodeState> nodes;

    XER::Engine::Audio::FX::CustomEffectPackage BuildPackage() const;

    // Persistence through XER::Engine::Cache (src/cache) — the rack configuration
    // survives application restarts.
    bool SaveToCache() const;
    bool LoadFromCache();
    std::string Serialize() const;
    bool Deserialize(const std::string& text);

    static std::vector<FxParamSpec> ParamSpecsFor(
        XER::Engine::Audio::FX::CustomEffectAlgorithm algorithm);
    static std::string AlgorithmDisplayName(
        XER::Engine::Audio::FX::CustomEffectAlgorithm algorithm);
    static bool SupportsWetMix(
        XER::Engine::Audio::FX::CustomEffectAlgorithm algorithm);
    static std::vector<XER::Engine::Audio::FX::CustomEffectAlgorithm> AvailableAlgorithms();
    static FxNodeState MakeDefaultNode(
        XER::Engine::Audio::FX::CustomEffectAlgorithm algorithm);
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_FX_RACK_STATE_H
