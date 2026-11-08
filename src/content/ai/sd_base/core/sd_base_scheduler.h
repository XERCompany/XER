#pragma once

#include "content/ai/sd_base/core/sd_base_types.h"

#include <cstdint>
#include <vector>

namespace XER::AI::SDBase {

class SdBaseScheduler {
public:
    void BuildSchedule(SdSchedulerType scheduler,
                       uint32_t steps,
                       float beta_start = 0.00085f,
                       float beta_end = 0.012f);

    uint32_t Steps() const { return steps_; }
    SdSchedulerType Scheduler() const { return scheduler_; }
    float BetaAt(uint32_t t) const;
    float AlphaCumProdAt(uint32_t t) const;
    float SigmaAt(uint32_t t) const;
    void ApplyStep(float* latent,
                   const float* predicted_noise,
                   uint64_t element_count,
                   uint32_t t,
                   float eta) const;

private:
    SdSchedulerType scheduler_ = SdSchedulerType::Euler;
    uint32_t steps_ = 0;
    std::vector<float> betas_;
    std::vector<float> alphas_cumprod_;
};

}  // namespace XER::AI::SDBase
