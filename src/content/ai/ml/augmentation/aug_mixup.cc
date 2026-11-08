#include "aug_mixup.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace XER::ML::Augmentation {

AugMixup::AugMixup(float alpha) : alpha_(alpha) {}

float AugMixup::SampleLambda() const {
    if (alpha_ <= 0.0f) return 1.0f;
    // Beta(alpha, alpha) approximated via inverse transform
    thread_local std::mt19937 rng{std::random_device{}()};
    std::gamma_distribution<float> gam(alpha_, 1.0f);
    float x = gam(rng), y = gam(rng);
    return x / (x + y + 1e-8f);
}

bool AugMixup::Blend(ImageSample& a, const ImageSample& b, float lambda) {
    if (!enabled_) return false;
    if (a.pixels.size() != b.pixels.size()) return false;
    if (lambda < 0.0f) lambda = SampleLambda();
    lambda = std::clamp(lambda, 0.0f, 1.0f);
    const float lam2 = 1.0f - lambda;
    for (size_t i = 0; i < a.pixels.size(); ++i)
        a.pixels[i] = a.pixels[i] * lambda + b.pixels[i] * lam2;
    return true;
}

}  // namespace XER::ML::Augmentation
