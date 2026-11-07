#include "waveform_widget.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "motion/accel.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Deterministic per-column hash so the "waveform" is stable between frames
// instead of shimmering noise.
float ColumnHash(int index) {
    unsigned int h = static_cast<unsigned int>(index) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}
} // namespace

WaveformWidget::WaveformWidget()
    : last_tick_(std::chrono::steady_clock::now()) {}

void WaveformWidget::SetProgress(float progress) {
    progress_ = std::clamp(progress, 0.0f, 1.0f);
}

void WaveformWidget::SetPlaying(bool playing) {
    playing_ = playing;
}

void WaveformWidget::Tick() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;
    phase_ += dt * (playing_ ? 2.4f : 0.6f);
    const float target = playing_ ? 1.0f : 0.18f;
    amplitude_ += (target - amplitude_) * std::min(1.0f, dt * 6.0f);
}

void WaveformWidget::Layout(const Layout::Rect& bounds) {
    bounds_ = bounds;
    SetBounds(bounds);
}

void WaveformWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const float width = bounds_.width;
    const float height = bounds_.height;
    if (width < 8.0f || height < 6.0f) {
        return;
    }

    const float mid_y = bounds_.y + height * 0.5f;
    const float bar_w = 3.0f;
    const float gap = 2.0f;
    const int bar_count = std::max(1, static_cast<int>((width - bar_w) / (bar_w + gap)));
    const float step = width / static_cast<float>(bar_count);
    const float envelope = 0.72f + 0.28f * std::sin(phase_ * 0.9f);
    const float progress_x = bounds_.x + width * progress_;

    renderer.FillRect(bounds_, tokens.muted, 1.0f);

    // Batch-evaluate the two sine terms for all bars at once — MKL VML
    // (SIMD) when linked, scalar libm otherwise. Layout of the scratch
    // buffer: [args_a | args_b | sin_a | sin_b], n floats each.
    const std::size_t n = static_cast<std::size_t>(bar_count);
    thread_local std::vector<float> work;
    work.resize(n * 4);
    float* arg_a = work.data();
    float* arg_b = arg_a + n;
    float* sin_a = arg_b + n;
    float* sin_b = sin_a + n;
    const float inv_n = 1.0f / static_cast<float>(bar_count);
    for (int i = 0; i < bar_count; ++i) {
        const float t = static_cast<float>(i) * inv_n;
        arg_a[i] = t * kPi * 6.0f + phase_ * 1.7f;
        arg_b[i] = t * kPi * 17.0f - phase_ * 2.3f + 1.3f;
    }
    Motion::Accel::Sin(arg_a, sin_a, n);
    Motion::Accel::Sin(arg_b, sin_b, n);

    for (int i = 0; i < bar_count; ++i) {
        const float x = bounds_.x + i * step + (step - bar_w) * 0.5f;
        // Layered sines + hash give an organic, music-like silhouette.
        float shape = 0.45f * sin_a[i] + 0.30f * sin_b[i] +
                      0.25f * ColumnHash(i);
        shape = std::abs(shape);
        const float bar_h = std::max(2.0f, shape * amplitude_ * envelope * height * 0.92f);
        const bool played = x + bar_w * 0.5f <= progress_x;
        const Theme::Color color = played ? tokens.accent : tokens.border;
        renderer.FillRect(
            Layout::Rect{x, mid_y - bar_h * 0.5f, bar_w, bar_h},
            color, 1.2f);
    }

    // Playhead line at the progress edge.
    if (progress_ > 0.001f && progress_ < 0.999f) {
        renderer.FillRect(
            Layout::Rect{progress_x - 1.0f, bounds_.y + 1.0f, 2.0f, height - 2.0f},
            tokens.accent, 1.0f);
    }
}

} // namespace XER::UI::Widgets
