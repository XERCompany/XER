// Waveform panel: horizontal audio-waveform preview for the selected
// clip — a zoomed-in counterpart to the tiny in-clip waveform the
// timeline draws.
//
//   ┌──────────────────────────────────────────────────┐
//   │  ▁▃▅▇█▇▅▃▁▁▃▅▇█▇▅▃▁   ▁▃▅▇█▇▅▃▁▁▃▅▇█▇▅▃▁       │
//   │  ▔▇▅▃▁▁▁▃▅▇█▇▅▃▁▔   ▔▇▅▃▁▁▁▃▅▇█▇▅▃▁▔▇▅▃       │
//   │ ─────────────────────────│────────────────────── │  playhead
//   └──────────────────────────────────────────────────┘
//
// The peaks map onto the clip's TIMELINE span: bucket space is the
// whole clip [clip_start, clip_start + duration) so the playhead —
// pushed in the same timeline seconds — lands where the timeline
// playhead sits on the clip. Min/max bars are mirrored around the
// vertical center, one vertical DrawLine per pixel column (the same
// idiom TimelineWidget::RenderWaveform uses). Fade-in/out handles are
// drawn as dimmed wedges with a diagonal gain ramp.
//
// Everything is drawn by the panel itself (no children); the mouse
// wheel scrolls horizontally when the zoomed waveform exceeds the
// viewport.
#pragma once

#include <string>
#include <vector>

#include "media_catalog.h"   // AudioPeaks (stored by value)
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui {

class WaveformPanelWidget final : public XER::UI::Widgets::Widget {
public:
    WaveformPanelWidget();

    // `peaks` is copied (channel-major min/max buckets, the layout
    // MediaCatalog::GetAudioPeaks produces). `clip_start`/`duration`
    // are timeline seconds the peaks span — the clip's timeline_start
    // and duration, so the playhead (same clock) aligns. An empty
    // peaks struct or duration <= 0 shows the empty state.
    void SetPeaks(const AudioPeaks& peaks, double clip_start,
                  double duration);
    void Clear();   // back to the empty state

    void SetZoom(double px_per_sec);         // clamped to [4, 2000]
    double Zoom() const { return px_per_second_; }
    // Horizontal scroll position, in timeline seconds at the view's
    // left edge (clamped to the clip's span when zoomed in).
    void SetScroll(double timeline_seconds);
    void SetPlayhead(double timeline_seconds);
    double Playhead() const { return playhead_; }
    // Fade handle lengths in seconds (clamped to the clip duration).
    void SetFadeIn(double seconds);
    void SetFadeOut(double seconds);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {220.0f, 80.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {480.0f, 140.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    float ClampScroll(float seconds) const;
    // Timeline seconds -> view x (before clipping).
    float XAt(double timeline_seconds) const;

    AudioPeaks peaks_;
    double clip_start_ = 0.0;   // timeline seconds
    double duration_ = 0.0;     // timeline seconds
    double px_per_second_ = 80.0;
    double scroll_seconds_ = 0.0;
    double playhead_ = 0.0;
    double fade_in_ = 0.0;
    double fade_out_ = 0.0;
};

}  // namespace XER::VideoEditor::Ui
