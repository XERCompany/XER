#include "synth_env_multistage.h"

#include <algorithm>

namespace XER::Audio::Synth {

void SynthEnvMultistage::SetStages(const std::vector<EnvStage>& stages) {
    stages_ = stages;
}

void SynthEnvMultistage::Trigger() {
    stage_index_ = 0;
}

float SynthEnvMultistage::Process(float sample_rate) {
    if (stages_.empty() || sample_rate <= 0.0f || stage_index_ >= stages_.size()) {
        return value_;
    }
    const EnvStage& s = stages_[stage_index_];
    const float step = (s.target - value_) / std::max(1.0f, s.time_s * sample_rate);
    value_ += step;
    if ((step >= 0.0f && value_ >= s.target) || (step < 0.0f && value_ <= s.target)) {
        value_ = s.target;
        ++stage_index_;
    }
    return value_;
}

}  // namespace XER::Audio::Synth
