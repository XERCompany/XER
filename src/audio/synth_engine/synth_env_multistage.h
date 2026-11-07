#pragma once

#include <vector>

namespace XER::Engine::Audio::Synth {

struct EnvStage { float target; float time_s; };

class SynthEnvMultistage {
public:
    void SetStages(const std::vector<EnvStage>& stages);
    void Trigger();
    float Process(float sample_rate);

private:
    std::vector<EnvStage> stages_;
    size_t stage_index_ = 0;
    float value_ = 0.0f;
};

}  // namespace XER::Engine::Audio::Synth
