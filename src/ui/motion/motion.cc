#include "motion.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace XER::UI::Motion {
namespace {

float Clamp01(float t) {
    return std::clamp(t, 0.0f, 1.0f);
}

} // namespace

float EaseOutCubic(float t) {
    t = Clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float EaseOutQuart(float t) {
    t = Clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u * u;
}

float EaseInOutCubic(float t) {
    t = Clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

float EaseOutBack(float t) {
    t = Clamp01(t);
    constexpr float c = 1.70158f;
    const float u = t - 1.0f;
    return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
}

bool ReducedMotion() {
#ifdef _WIN32
    static const bool reduced = [] {
        BOOL on = TRUE;
        // SPI_GETCLIENTAREAANIMATION = "Animate controls and elements
        // inside windows" (Settings > Accessibility > Visual effects).
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
        return on == FALSE;
    }();
    return reduced;
#else
    return false;
#endif
}

bool Spring::Update(float dt_seconds) {
    if (dt_seconds <= 0.0f) {
        return !Settled();
    }
    if (ReducedMotion() || stiffness <= 0.0f) {
        Snap(target);
        return false;
    }
    // Substep at ~120Hz so the 33ms frame tick stays numerically stable.
    const int steps = std::max(1, static_cast<int>(std::ceil(dt_seconds / 0.008f)));
    const float h = dt_seconds / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        const float accel = -stiffness * (pos - target) - damping * vel;
        vel += accel * h;
        pos += vel * h;
    }
    if (std::fabs(pos - target) < 0.0005f && std::fabs(vel) < 0.0005f) {
        pos = target;
        vel = 0.0f;
    }
    return !Settled();
}

bool Spring::Settled() const {
    return pos == target && vel == 0.0f;
}

void Tween::Set(float target, float dur_seconds, EaseFn fn) {
    if (ReducedMotion()) {
        from = to = target;
        t = 1.0f;
        duration = dur_seconds;
        ease = fn;
        return;
    }
    from = Value();
    to = target;
    t = 0.0f;
    duration = dur_seconds > 0.0f ? dur_seconds : 0.0001f;
    ease = fn != nullptr ? fn : &EaseOutCubic;
}

float Tween::Update(float dt_seconds) {
    if (t >= 1.0f) {
        return to;
    }
    t = std::min(1.0f, t + dt_seconds / duration);
    return Value();
}

float Tween::Value() const {
    if (t >= 1.0f) {
        return to;
    }
    const float e = ease != nullptr ? ease(t) : t;
    return from + (to - from) * e;
}

} // namespace XER::UI::Motion
