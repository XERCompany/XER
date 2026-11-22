#include "waveform_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

constexpr double kMinZoom = 4.0;
constexpr double kMaxZoom = 2000.0;

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// MM:SS.mmm for the small corner readout.
std::string FormatShortTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long millis =
        static_cast<long long>(seconds * 1000.0 + 0.5);
    const long long minutes = millis / 60000;
    const long long secs = (millis / 1000) % 60;
    const long long ms = millis % 1000;
    char buffer[20];
    std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld.%03lld",
                  minutes, secs, ms);
    return buffer;
}

}  // namespace

WaveformPanelWidget::WaveformPanelWidget() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void WaveformPanelWidget::SetPeaks(const AudioPeaks& peaks,
                                   double clip_start, double duration) {
    peaks_ = peaks;
    clip_start_ = std::isfinite(clip_start) ? clip_start : 0.0;
    duration_ = std::isfinite(duration) ? std::max(0.0, duration) : 0.0;
    fade_in_ = std::clamp(fade_in_, 0.0, duration_);
    fade_out_ = std::clamp(fade_out_, 0.0, duration_);
    scroll_seconds_ = ClampScroll(static_cast<float>(scroll_seconds_));
    Invalidate();
}

void WaveformPanelWidget::Clear() {
    peaks_ = AudioPeaks{};
    duration_ = 0.0;
    Invalidate();
}

void WaveformPanelWidget::SetZoom(double px_per_sec) {
    if (!std::isfinite(px_per_sec)) return;
    px_per_second_ = std::clamp(px_per_sec, kMinZoom, kMaxZoom);
    scroll_seconds_ = ClampScroll(static_cast<float>(scroll_seconds_));
    Invalidate();
}

void WaveformPanelWidget::SetScroll(double timeline_seconds) {
    if (!std::isfinite(timeline_seconds)) return;
    scroll_seconds_ = ClampScroll(static_cast<float>(timeline_seconds));
    Invalidate();
}

void WaveformPanelWidget::SetPlayhead(double timeline_seconds) {
    const double t = std::isfinite(timeline_seconds) ? timeline_seconds : 0.0;
    if (t == playhead_) return;
    playhead_ = t;
    Invalidate();
}

void WaveformPanelWidget::SetFadeIn(double seconds) {
    fade_in_ = std::isfinite(seconds)
                   ? std::clamp(seconds, 0.0, duration_)
                   : 0.0;
    Invalidate();
}

void WaveformPanelWidget::SetFadeOut(double seconds) {
    fade_out_ = std::isfinite(seconds)
                    ? std::clamp(seconds, 0.0, duration_)
                    : 0.0;
    Invalidate();
}

float WaveformPanelWidget::ClampScroll(float seconds) const {
    const float visible =
        static_cast<float>(GetBounds().width) /
        static_cast<float>(std::max(px_per_second_, 1.0));
    const float lo = static_cast<float>(clip_start_);
    const float hi = std::max(
        lo, static_cast<float>(clip_start_ + duration_) - visible);
    return std::clamp(seconds, lo, hi);
}

float WaveformPanelWidget::XAt(double timeline_seconds) const {
    return GetBounds().x +
           static_cast<float>((timeline_seconds - scroll_seconds_) *
                              px_per_second_);
}

void WaveformPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const Rect bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);

    const bool has_peaks = peaks_.buckets > 0 && peaks_.channels > 0 &&
                           duration_ > 0.0 &&
                           peaks_.data.size() >=
                               peaks_.buckets * 2u *
                                   static_cast<std::size_t>(
                                       std::max(1, peaks_.channels));
    if (!has_peaks) {
        renderer.DrawText("no audio waveform — select a clip with audio",
                          {bounds.x + 12.0f,
                           bounds.y + bounds.height * 0.5f - 10.0f,
                           bounds.width - 24.0f, 20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    // Wave area leaves a strip at the bottom for the corner readout.
    const Rect wave{bounds.x + 2.0f, bounds.y + 6.0f,
                    bounds.width - 4.0f, bounds.height - 26.0f};
    const float mid = wave.y + wave.height * 0.5f;
    const float half = wave.height * 0.46f;

    // Mirrored min/max bars: one vertical line per pixel column, the
    // same scheme TimelineWidget::RenderWaveform uses — buckets are
    // channel-major min/max pairs spread over the clip's span.
    renderer.PushClip(wave);
    const int columns = std::max(1, static_cast<int>(wave.width));
    const std::size_t buckets = peaks_.buckets;
    const int channels = peaks_.channels;
    const Color color = WithAlpha(tokens.accent, 0.7f);
    const double span0 = clip_start_;
    const double span1 = clip_start_ + duration_;
    for (int px = 0; px < columns; ++px) {
        const double t0 =
            scroll_seconds_ + static_cast<double>(px) / px_per_second_;
        const double t1 =
            scroll_seconds_ +
            static_cast<double>(px + 1) / px_per_second_;
        const double u0 = (t0 - span0) / duration_;
        const double u1 = (t1 - span0) / duration_;
        if (u1 <= 0.0 || u0 >= 1.0) continue;
        int b0 = static_cast<int>(std::floor(u0 * buckets));
        int b1 = static_cast<int>(std::ceil(u1 * buckets)) - 1;
        b0 = std::clamp(b0, 0, static_cast<int>(buckets) - 1);
        b1 = std::clamp(b1, b0, static_cast<int>(buckets) - 1);
        float mn = 1.0f, mx = -1.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const std::size_t base =
                static_cast<std::size_t>(ch) * buckets * 2u;
            for (int b = b0; b <= b1; ++b) {
                mn = std::min(mn, peaks_.data[base +
                                              static_cast<std::size_t>(b) * 2u]);
                mx = std::max(mx, peaks_.data[base +
                                              static_cast<std::size_t>(b) * 2u + 1u]);
            }
        }
        if (!(mn <= mx)) {
            mn = 0.0f;
            mx = 0.0f;
        }
        const float x = wave.x + static_cast<float>(px) + 0.5f;
        float y0 = mid - std::clamp(mx, -1.0f, 1.0f) * half;
        float y1 = mid - std::clamp(mn, -1.0f, 1.0f) * half;
        if (y1 - y0 < 1.0f) {
            y0 = mid - 0.5f;
            y1 = mid + 0.5f;
        }
        renderer.DrawLine({x, y0}, {x, y1}, color, 1.0f);
    }
    // Center line.
    renderer.DrawLine({wave.x, mid}, {wave.Right(), mid},
                      WithAlpha(tokens.border, 0.8f), 1.0f);

    // Fade wedges: dim the faded region and draw the gain ramp line —
    // fade-in rises over the first fade_in_ seconds, fade-out falls
    // over the last fade_out_ seconds.
    const auto draw_fade = [&](double from_t, double to_t, bool rising) {
        if (!(to_t > from_t)) return;
        const float x0 = XAt(from_t);
        const float x1 = XAt(to_t);
        if (x1 <= wave.x || x0 >= wave.Right()) return;
        const Rect dim{std::max(x0, wave.x), wave.y,
                       std::min(x1, wave.Right()) - std::max(x0, wave.x),
                       wave.height};
        renderer.FillRect(dim, WithAlpha(tokens.surface, 0.45f), 0.0f);
        renderer.DrawLine(
            {x0, rising ? mid + half : mid - half},
            {x1, rising ? mid - half : mid + half},
            tokens.accent, 1.5f);
    };
    draw_fade(span0, span0 + fade_in_, /*rising=*/true);
    draw_fade(span1 - fade_out_, span1, /*rising=*/false);

    // Playhead (timeline seconds — it only shows while inside the
    // clip's span, matching the timeline's own playhead position).
    if (playhead_ >= span0 && playhead_ <= span1) {
        const float x = XAt(playhead_);
        renderer.DrawLine({x, wave.y}, {x, wave.Bottom()},
                          tokens.accent, 1.5f);
    }
    renderer.PopClip();

    // Corner readout: clip span + zoom.
    char readout[64];
    std::snprintf(readout, sizeof(readout), "%s \xc2\xb7 %s \xc2\xb7 %d px/s",
                  FormatShortTime(clip_start_).c_str(),
                  FormatShortTime(duration_).c_str(),
                  static_cast<int>(px_per_second_));
    renderer.DrawText(readout,
                      {bounds.x + 10.0f,
                       bounds.y + bounds.height - 20.0f,
                       bounds.width - 20.0f, 14.0f},
                      XER::UI::Rendering::TextStyle{
                          tokens.muted, tokens.font_small, 400});
}

void WaveformPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    scroll_seconds_ = ClampScroll(static_cast<float>(scroll_seconds_));
}

bool WaveformPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    if (event.type != XER::UI::Widgets::Event::Type::MouseWheel) {
        return false;
    }
    // Wheel scrolls horizontally (same scale as the sibling panels);
    // zoom stays host-driven via SetZoom.
    const double step = static_cast<double>(event.wheel_delta) /
                        (120.0 * px_per_second_) * 60.0;
    const float next =
        ClampScroll(static_cast<float>(scroll_seconds_ - step));
    if (next == static_cast<float>(scroll_seconds_)) return false;
    scroll_seconds_ = next;
    Invalidate();
    return true;
}

void WaveformPanelWidget::Tick(double dt_seconds) {
    (void)dt_seconds;
}

}  // namespace XER::VideoEditor::Ui
