#include "waveform_widget.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "motion/accel.h"
#include "motion/motion.h"
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

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a *= alpha;
    return color;
}

constexpr float kBarWidth = 3.0f;
constexpr float kBarGap = 1.0f;
} // namespace

WaveformWidget::WaveformWidget()
    : last_tick_(std::chrono::steady_clock::now()) {}

// ---- Decorative-mode API (unchanged semantics) ----

void WaveformWidget::SetProgress(float progress) {
    progress_ = std::clamp(progress, 0.0f, 1.0f);
    // Keep the peak-mode playhead in sync when peaks are loaded so
    // SetProgress callers (AudioPlayerWidget, addon bindings) drive both.
    if (!peaks_.empty()) {
        position_s_ = progress_ * domain_end_s_;
    }
    Invalidate();
}

void WaveformWidget::SetPlaying(bool playing) {
    playing_ = playing;
}

void WaveformWidget::Tick() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;
    Advance(dt);
}

void WaveformWidget::Tick(double dt_seconds) {
    // Share the wall clock with the no-arg Tick() so hosts that drive both
    // paths (manual Tick + tree TickTree) advance ~once per frame total.
    last_tick_ = std::chrono::steady_clock::now();
    Advance(static_cast<float>(dt_seconds));
}

void WaveformWidget::Advance(float dt) {
    // Decorative sine animation only — peak mode is data-driven and has
    // no ambient motion. Honors ReducedMotion by freezing the phase.
    const float target = playing_ ? 1.0f : 0.18f;
    if (Motion::ReducedMotion()) {
        amplitude_ = target;
        return;
    }
    phase_ += dt * (playing_ ? 2.4f : 0.6f);
    amplitude_ += (target - amplitude_) * std::min(1.0f, dt * 6.0f);
}

// ---- Peak-mode API ----

void WaveformWidget::SetPeaks(std::vector<std::pair<float, float>> peaks,
                              double duration_seconds) {
    peaks_ = std::move(peaks);
    domain_end_s_ = duration_seconds > 0.0 ? duration_seconds : 1.0;
    range_start_s_ = 0.0;
    range_end_s_ = domain_end_s_;
    position_s_ = progress_ * domain_end_s_;
    ApplyMinPxPerSec();
    // Interactive only with real data — decorative mode stays inert for
    // back-compat (HandleEvent returned false before this upgrade).
    SetFocusable(!peaks_.empty());
    Invalidate();
}

void WaveformWidget::ClearPeaks() {
    peaks_.clear();
    regions_.clear();
    markers_.clear();
    SetFocusable(false);
    Invalidate();
}

void WaveformWidget::SetRange(double start_s, double end_s) {
    if (end_s < start_s) {
        std::swap(start_s, end_s);
    }
    const double min_span = peaks_.empty()
        ? 1e-4
        : std::max(domain_end_s_ * 8.0 / static_cast<double>(peaks_.size()),
                   1e-4);
    const double span = std::max(end_s - start_s, min_span);
    start_s = std::clamp(start_s, 0.0, std::max(0.0, domain_end_s_ - span));
    range_start_s_ = start_s;
    range_end_s_ = std::min(domain_end_s_, start_s + span);
    Invalidate();
}

void WaveformWidget::SetShape(Shape shape) {
    shape_ = shape;
    Invalidate();
}

void WaveformWidget::SetMinPxPerSec(float px_per_sec) {
    px_per_sec_ = std::max(0.0f, px_per_sec);
    ApplyMinPxPerSec();
}

void WaveformWidget::ApplyMinPxPerSec() {
    if (px_per_sec_ <= 0.0f || peaks_.empty() || bounds_.width <= 0.0f) {
        return;
    }
    const double span = static_cast<double>(bounds_.width) / px_per_sec_;
    SetRange(range_start_s_, range_start_s_ + span);
}

void WaveformWidget::SetPosition(double time_s) {
    position_s_ = time_s;
    if (domain_end_s_ > 0.0) {
        progress_ = static_cast<float>(std::clamp(time_s / domain_end_s_, 0.0, 1.0));
    }
    Invalidate();
}

void WaveformWidget::SetRegions(std::vector<Region> regions) {
    regions_ = std::move(regions);
    Invalidate();
}

void WaveformWidget::SetMarkers(std::vector<Marker> markers) {
    markers_ = std::move(markers);
    Invalidate();
}

void WaveformWidget::SetOnSeek(std::function<void(double)> callback) {
    on_seek_ = std::move(callback);
}

void WaveformWidget::SetOnRangeSelect(
    std::function<void(double, double)> callback) {
    on_range_select_ = std::move(callback);
}

double WaveformWidget::TimeFromX(float x, const Layout::Rect& bounds) const {
    const float w = std::max(bounds.width, 1.0f);
    const double t = std::clamp(
        static_cast<double>((x - bounds.x) / w), 0.0, 1.0);
    return range_start_s_ + t * (range_end_s_ - range_start_s_);
}

float WaveformWidget::XFromTime(double t, const Layout::Rect& bounds) const {
    const double span = std::max(range_end_s_ - range_start_s_, 1e-9);
    return bounds.x + static_cast<float>((t - range_start_s_) / span) *
           bounds.width;
}

void WaveformWidget::SeekTo(float x, bool fire_callback) {
    const double t = TimeFromX(x, bounds_);
    SetPosition(t);
    if (fire_callback && on_seek_) {
        on_seek_(t);
    }
}

// Aggregated {min,max} over the peak slices covering [t0,t1] — the
// canvas resamples the stored slices to width (audio-ui computePeaks).
std::pair<float, float> WaveformWidget::PeaksAt(double t0, double t1) const {
    const double n = static_cast<double>(peaks_.size());
    if (n <= 0.0 || domain_end_s_ <= 0.0) {
        return {0.0f, 0.0f};
    }
    const double inv = n / domain_end_s_;
    int i0 = static_cast<int>(std::floor(t0 * inv));
    int i1 = static_cast<int>(std::ceil(t1 * inv));
    i0 = std::clamp(i0, 0, static_cast<int>(n) - 1);
    i1 = std::clamp(i1, i0 + 1, static_cast<int>(n));
    float mn = peaks_[static_cast<std::size_t>(i0)].first;
    float mx = peaks_[static_cast<std::size_t>(i0)].second;
    for (int i = i0 + 1; i < i1; ++i) {
        mn = std::min(mn, peaks_[static_cast<std::size_t>(i)].first);
        mx = std::max(mx, peaks_[static_cast<std::size_t>(i)].second);
    }
    return {std::clamp(mn, -1.0f, 1.0f), std::clamp(mx, -1.0f, 1.0f)};
}

void WaveformWidget::Layout(const Layout::Rect& bounds) {
    bounds_ = bounds;
    SetBounds(bounds);
    ApplyMinPxPerSec();
}

void WaveformWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const float width = bounds_.width;
    const float height = bounds_.height;
    if (width < 8.0f || height < 6.0f) {
        return;
    }
    if (peaks_.empty()) {
        RenderDecorative(renderer, tokens);
    } else {
        RenderPeaks(renderer, tokens);
    }
    RenderFocusRing(renderer, tokens, 1.0f);
}

void WaveformWidget::RenderDecorative(Rendering::Renderer& renderer,
                                      const Theme::Tokens& tokens) {
    const float width = bounds_.width;
    const float height = bounds_.height;
    const float mid_y = bounds_.y + height * 0.5f;
    const float bar_w = 3.0f;
    const float gap = 2.0f;
    const int bar_count = std::max(1, static_cast<int>((width - bar_w) / (bar_w + gap)));
    const float step = width / static_cast<float>(bar_count);
    const float envelope = 0.72f + 0.28f * std::sin(phase_ * 0.9f);
    const float progress_x = bounds_.x + width * progress_;

    // Recessed lane for the waveform (muted is a text color — as a fill it
    // paints a glaring gray slab in dark themes).
    renderer.FillRect(bounds_, tokens.secondary, 1.0f);

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

void WaveformWidget::RenderPeaks(Rendering::Renderer& renderer,
                                 const Theme::Tokens& tokens) {
    const float width = bounds_.width;
    const float height = bounds_.height;
    const float mid_y = bounds_.y + height * 0.5f;
    const float half_h = height * 0.5f - 1.0f;
    const bool enabled = IsEnabled();
    const double span = range_end_s_ - range_start_s_;

    // Recessed lane + region underlays.
    renderer.FillRect(bounds_, tokens.secondary, 1.0f);
    for (const auto& region : regions_) {
        const double lo = std::min(region.start_s, region.end_s);
        const double hi = std::max(region.start_s, region.end_s);
        if (hi <= range_start_s_ || lo >= range_end_s_) {
            continue;
        }
        const float x0 = XFromTime(std::max(lo, range_start_s_), bounds_);
        const float x1 = XFromTime(std::min(hi, range_end_s_), bounds_);
        // Selection reads as list-selection tint; loop as an accent wash.
        const Theme::Color tint = region.role == RegionRole::Loop
            ? WithAlpha(tokens.accent, 0.25f)
            : tokens.selection;
        renderer.FillRect({x0, bounds_.y, x1 - x0, height}, tint, 0.0f);
    }

    const Theme::Color wave_color =
        enabled ? tokens.waveform : tokens.muted;
    const Theme::Color played_color =
        enabled ? tokens.accent : tokens.muted;
    const double pos = position_s_;

    if (shape_ == Shape::Line) {
        // Two polylines (top = max, bottom = min), split at the playhead.
        const int columns = std::max(1, static_cast<int>(width / 2.0f));
        Layout::Point prev_top{}, prev_bot{};
        for (int i = 0; i <= columns; ++i) {
            const double t0 = range_start_s_ + span * i / columns;
            const double t1 = range_start_s_ + span * (i + 1.0) / columns;
            const auto [mn, mx] = PeaksAt(t0, t1);
            const float x = bounds_.x + width * i / columns;
            const Layout::Point top{x, mid_y - mx * half_h};
            const Layout::Point bot{x, mid_y - mn * half_h};
            if (i > 0) {
                const Theme::Color c = t0 < pos ? played_color : wave_color;
                renderer.DrawLine(prev_top, top, c, 1.0f);
                renderer.DrawLine(prev_bot, bot, c, 1.0f);
            }
            prev_top = top;
            prev_bot = bot;
        }
    } else if (shape_ == Shape::Bars) {
        const int bar_count = std::max(
            1, static_cast<int>(width / (kBarWidth + kBarGap)));
        const float step = width / static_cast<float>(bar_count);
        for (int i = 0; i < bar_count; ++i) {
            const double t0 = range_start_s_ + span * i / bar_count;
            const double t1 = range_start_s_ + span * (i + 1.0) / bar_count;
            const auto [mn, mx] = PeaksAt(t0, t1);
            const float x = bounds_.x + i * step + (step - kBarWidth) * 0.5f;
            const float y0 = mid_y - mx * half_h;
            const float y1 = mid_y - mn * half_h;
            const Theme::Color c =
                (t0 + t1) * 0.5 < pos ? played_color : wave_color;
            renderer.FillRect({x, y0, kBarWidth, std::max(1.0f, y1 - y0)},
                              c, kBarWidth * 0.5f);
        }
    } else {  // Mirror — per-pixel min/max columns.
        const int columns = std::max(1, static_cast<int>(width));
        for (int i = 0; i < columns; ++i) {
            const double t0 = range_start_s_ + span * i / columns;
            const double t1 = range_start_s_ + span * (i + 1.0) / columns;
            const auto [mn, mx] = PeaksAt(t0, t1);
            const float y0 = mid_y - mx * half_h;
            const float y1 = mid_y - mn * half_h;
            const Theme::Color c =
                (t0 + t1) * 0.5 < pos ? played_color : wave_color;
            renderer.FillRect(
                {bounds_.x + static_cast<float>(i), y0, 1.0f,
                 std::max(1.0f, y1 - y0)},
                c, 0.0f);
        }
    }

    // Cue markers over the trace.
    for (const auto& marker : markers_) {
        if (marker.time_s < range_start_s_ ||
            marker.time_s > range_end_s_) {
            continue;
        }
        const float x = XFromTime(marker.time_s, bounds_);
        renderer.DrawLine({x, bounds_.y + 1.0f},
                          {x, bounds_.y + height - 1.0f},
                          tokens.warning_chip, 1.0f);
        if (!marker.label.empty()) {
            renderer.DrawText(
                marker.label,
                {x + 3.0f, bounds_.y + 1.0f, 80.0f, 12.0f},
                Rendering::TextStyle{tokens.timecode,
                                     tokens.font_small, 500, true});
        }
    }

    // In-progress Shift+drag selection.
    if (selecting_) {
        const double lo = std::min(sel_anchor_s_, sel_current_s_);
        const double hi = std::max(sel_anchor_s_, sel_current_s_);
        const float x0 = XFromTime(lo, bounds_);
        const float x1 = XFromTime(hi, bounds_);
        renderer.FillRect({x0, bounds_.y, x1 - x0, height},
                          WithAlpha(tokens.accent, 0.18f), 0.0f);
    }

    // Playhead — hidden when the position is outside the visible range
    // (audio-ui `data-outside`) instead of pinning to an edge.
    if (pos >= range_start_s_ && pos <= range_end_s_) {
        const float x = XFromTime(pos, bounds_);
        renderer.FillRect({x - 1.0f, bounds_.y + 1.0f, 2.0f, height - 2.0f},
                          tokens.playhead, 1.0f);
    }

    // Hover scrub line.
    if (enabled && IsHovered() && hover_x_ >= 0.0f) {
        const float x = bounds_.x + hover_x_;
        renderer.DrawLine({x, bounds_.y + 1.0f},
                          {x, bounds_.y + height - 1.0f},
                          WithAlpha(tokens.foreground, 0.35f), 1.0f);
    }
}

bool WaveformWidget::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        // A widget disabled mid-gesture must not keep scrubbing.
        if (scrubbing_ || selecting_) {
            scrubbing_ = false;
            selecting_ = false;
            SetActive(false);
        }
        return false;
    }
    if (peaks_.empty()) {
        // Decorative mode keeps its historic contract: fully inert.
        return false;
    }

    switch (event.type) {
    case Event::Type::MouseDown:
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        if (event.is_shift) {
            selecting_ = true;
            sel_anchor_s_ = sel_current_s_ = TimeFromX(event.position.x, bounds_);
            Invalidate();
            return true;
        }
        scrubbing_ = true;
        SetActive(true);
        SeekTo(event.position.x, true);
        return true;

    case Event::Type::MouseMove:
        hover_x_ = event.position.x - bounds_.x;
        if (scrubbing_ && event.mouse_button == MouseButton::Left) {
            SeekTo(event.position.x, true);
            return true;
        }
        if (selecting_) {
            sel_current_s_ = TimeFromX(event.position.x, bounds_);
            Invalidate();
            return true;
        }
        Invalidate();  // hover line follows the pointer
        return false;

    case Event::Type::MouseUp:
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        if (selecting_) {
            selecting_ = false;
            const double lo = std::min(sel_anchor_s_, sel_current_s_);
            const double hi = std::max(sel_anchor_s_, sel_current_s_);
            if (on_range_select_ && hi - lo > 1e-9) {
                on_range_select_(lo, hi);
            }
            Invalidate();
            return true;
        }
        if (scrubbing_) {
            scrubbing_ = false;
            SetActive(false);
            return true;
        }
        return false;

    case Event::Type::MouseWheel: {
        // Wheel zoom only while focused — page scroll never changes a
        // zoom range by accident (tactile/cross-site convention).
        if (!IsFocused()) {
            return false;
        }
        const double notches = event.wheel_delta / 120.0;
        const double factor = std::pow(1.25, -notches);
        const double old_span = range_end_s_ - range_start_s_;
        const double t_p = TimeFromX(event.position.x, bounds_);
        const double frac =
            old_span > 0.0 ? (t_p - range_start_s_) / old_span : 0.5;
        const double new_span = old_span * factor;
        SetRange(t_p - frac * new_span, t_p + (1.0 - frac) * new_span);
        return true;
    }

    case Event::Type::KeyDown:
        if (!IsFocused()) {
            return false;
        }
        switch (event.key) {
        case KeyCode::ArrowLeft:
        case KeyCode::ArrowRight: {
            const double step = (range_end_s_ - range_start_s_) * 0.05;
            const double t = event.key == KeyCode::ArrowLeft
                ? position_s_ - step : position_s_ + step;
            SetPosition(std::clamp(t, 0.0, domain_end_s_));
            if (on_seek_) {
                on_seek_(position_s_);
            }
            return true;
        }
        case KeyCode::Home:
            SetPosition(0.0);
            if (on_seek_) on_seek_(position_s_);
            return true;
        case KeyCode::End:
            SetPosition(domain_end_s_);
            if (on_seek_) on_seek_(position_s_);
            return true;
        default:
            return false;
        }

    case Event::Type::Blur:
        scrubbing_ = false;
        selecting_ = false;
        SetActive(false);
        return false;

    default:
        return false;
    }
}

} // namespace XER::UI::Widgets
