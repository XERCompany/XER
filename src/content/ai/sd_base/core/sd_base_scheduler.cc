#include "sd_base_scheduler.h"

#include <algorithm>
#include <cmath>

namespace XER::AI::SDBase {

void SdBaseScheduler::BuildSchedule(SdSchedulerType scheduler,
                                    uint32_t steps,
                                    float beta_start,
                                    float beta_end) {
    scheduler_ = scheduler;
    steps_ = std::max<uint32_t>(1, steps);
    betas_.assign(steps_, 0.0f);
    alphas_cumprod_.assign(steps_, 0.0f);

    const float step_inv = (steps_ > 1) ? 1.0f / static_cast<float>(steps_ - 1) : 0.0f;
    float running = 1.0f;

    for (uint32_t i = 0; i < steps_; ++i) {
        const float u = static_cast<float>(i) * step_inv;
        const float beta = beta_start + (beta_end - beta_start) * u;
        betas_[i] = std::clamp(beta, 1e-6f, 0.999f);

        const float alpha = 1.0f - betas_[i];
        running *= alpha;
        alphas_cumprod_[i] = std::clamp(running, 1e-8f, 1.0f);
    }
}

float SdBaseScheduler::BetaAt(uint32_t t) const {
    if (betas_.empty()) return 0.0f;
    const uint32_t idx = std::min<uint32_t>(t, static_cast<uint32_t>(betas_.size() - 1));
    return betas_[idx];
}

float SdBaseScheduler::AlphaCumProdAt(uint32_t t) const {
    if (alphas_cumprod_.empty()) return 1.0f;
    const uint32_t idx = std::min<uint32_t>(t, static_cast<uint32_t>(alphas_cumprod_.size() - 1));
    return alphas_cumprod_[idx];
}

float SdBaseScheduler::SigmaAt(uint32_t t) const {
    const float a = AlphaCumProdAt(t);
    const float one_minus_a = std::max(1e-8f, 1.0f - a);
    return std::sqrt(one_minus_a / a);
}

void SdBaseScheduler::ApplyStep(float* latent,
                                const float* predicted_noise,
                                uint64_t element_count,
                                uint32_t t,
                                float eta) const {
    if (latent == nullptr || predicted_noise == nullptr || element_count == 0) return;
    const float alpha_t = AlphaCumProdAt(t);
    const float alpha_prev = (t + 1 < steps_) ? AlphaCumProdAt(t + 1) : AlphaCumProdAt(t);
    const float sigma_t = SigmaAt(t);
    const float sigma_next = (t + 1 < steps_) ? SigmaAt(t + 1) : 0.0f;

    if (scheduler_ == SdSchedulerType::DDIM) {
        const float sqrt_alpha_t = std::sqrt(std::max(1e-8f, alpha_t));
        const float sqrt_one_minus_alpha_t = std::sqrt(std::max(1e-8f, 1.0f - alpha_t));
        const float sqrt_alpha_prev = std::sqrt(std::max(1e-8f, alpha_prev));
        const float sqrt_one_minus_alpha_prev = std::sqrt(std::max(1e-8f, 1.0f - alpha_prev));
        const float eta_term = std::clamp(eta, 0.0f, 1.0f) * 0.01f;

        for (uint64_t i = 0; i < element_count; ++i) {
            const float x_t = latent[i];
            const float eps = predicted_noise[i];
            const float x0 = (x_t - sqrt_one_minus_alpha_t * eps) / sqrt_alpha_t;
            const float dir = sqrt_one_minus_alpha_prev * eps;
            latent[i] = sqrt_alpha_prev * x0 + dir + eta_term * eps;
        }
        return;
    }

    // Euler ancestral-like update.
    const float dt = std::max(1e-4f, sigma_t - sigma_next);
    for (uint64_t i = 0; i < element_count; ++i) {
        latent[i] -= dt * predicted_noise[i];
    }
}

}  // namespace XER::AI::SDBase
