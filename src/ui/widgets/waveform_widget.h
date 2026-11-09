#ifndef XER_UI_WIDGETS_WAVEFORM_WIDGET_H
#define XER_UI_WIDGETS_WAVEFORM_WIDGET_H

#include <chrono>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Animated waveform strip: procedural bars driven by layered sines plus a
// per-column hash, breathing with playback state. Progress (0..1) paints the
// played region in the accent color — doubles as a seek-position indicator.
class WaveformWidget : public Widget {
public:
    WaveformWidget();

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {80.0f, 18.0f}; }
    Layout::Size GetPreferredSize() const override { return {240.0f, 26.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

    void SetProgress(float progress);
    void SetPlaying(bool playing);
    void Tick();

private:
    Layout::Rect bounds_;
    float progress_ = 0.0f;
    float amplitude_ = 0.15f;
    bool playing_ = false;
    float phase_ = 0.0f;
    std::chrono::steady_clock::time_point last_tick_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_WAVEFORM_WIDGET_H
