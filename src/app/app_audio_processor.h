#pragma once

#include <string>
#include <vector>

namespace XER::App {

// Live audio effect application
class AudioProcessor {
public:
    AudioProcessor();
    ~AudioProcessor();

    // Initialize audio processor
    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    // Apply effect in real-time
    bool ApplyEffectLive(const std::string& effect_name, float param1 = 0.0f, float param2 = 0.0f);
    
    // Apply preset
    bool ApplyPreset(const std::string& preset_name);
    
    // Get available effects
    std::vector<std::string> GetAvailableEffects() const;
    
    // Get available presets
    std::vector<std::string> GetAvailablePresets() const;

private:
    bool initialized_;
};

}  // namespace XER::App