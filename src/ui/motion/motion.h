#ifndef XER_UI_MOTION_MOTION_H
#define XER_UI_MOTION_MOTION_H

// Motion primitives for the widget layer, following the XER Design motion
// rules: springs and short tweens on a strong ease-out, exits quicker than
// entrances, transforms/opacity rather than layout, interruptible, and no
// animation on keyboard navigation. Reduced motion is respected everywhere.

namespace XER::UI::Motion {

// ── Easing curves ──────────────────────────────────────────────────────────
// All take t in [0,1] and return an eased value (may overshoot for Back).

float EaseOutCubic(float t);
float EaseOutQuart(float t);
float EaseInOutCubic(float t);
float EaseOutBack(float t);

// True when the user disabled "Animation effects" in Windows settings
// (SystemParametersInfo SPI_GETCLIENTAREAANIMATION). Result is cached.
bool ReducedMotion();

// ── Spring ─────────────────────────────────────────────────────────────────
// Semi-implicit Euler spring, substepped for stability on ~30fps ticks.
// pos chases target with a slight settle; vel is preserved across retargets
// so interrupted animations stay smooth.
struct Spring {
    float pos = 0.0f;
    float vel = 0.0f;
    float target = 0.0f;
    // Defaults give a quick ~250ms settle with a whisper of overshoot.
    float stiffness = 380.0f;
    float damping = 30.0f;

    void SetTarget(float v) { target = v; }
    void Snap(float v) { pos = v; target = v; vel = 0.0f; }
    // Advances by dt_seconds; returns true while still in motion.
    bool Update(float dt_seconds);
    bool Settled() const;
};

// ── Tween ──────────────────────────────────────────────────────────────────
// Timed eased transition between two values. Interruptible: Set() retargets
// from the current value, so rapid state flips never jump.
struct Tween {
    using EaseFn = float (*)(float);

    float from = 0.0f;
    float to = 0.0f;
    float t = 1.0f;         // progress 0..1
    float duration = 0.16f; // seconds
    EaseFn ease = &EaseOutCubic;

    // Starts (or retargets) a tween toward `target` over `dur_seconds`.
    // Reduced motion snaps instantly.
    void Set(float target, float dur_seconds = 0.16f,
             EaseFn fn = &EaseOutCubic);
    // Advances and returns the current value.
    float Update(float dt_seconds);
    float Value() const;
    bool Active() const { return t < 1.0f; }
};

// Linear interpolation helper for colors done per-widget (kept here so every
// widget lerps identically).
template <typename Color>
Color LerpColor(const Color& a, const Color& b, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return Color{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
}

} // namespace XER::UI::Motion

#endif // XER_UI_MOTION_MOTION_H
