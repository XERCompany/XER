// Broadcast-style video scopes for the XER video editor program
// monitor — waveform (luma), RGB parade, per-channel histogram and a
// polar Cb/Cr vectorscope — all computed from the monitor's packed
// BGRA8 frame and painted via Renderer::DrawFrameBgra.
//
// Feed with SetFrame() whenever the monitor produces or refreshes a
// frame; recomputation is throttled internally (>= ~100 ms between
// computes, plus forced updates on mode/source changes) so scope math
// stays off the hot paint path. A small mode-switch strip along the
// top edge is hit-tested in HandleEvent().
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "widgets/widget.h"

namespace XER::VideoEditor::Ui {

class ScopesWidget final : public XER::UI::Widgets::Widget {
public:
    enum class Mode {
        Waveform,     // luma: column x = luma histogram of source column x
        RgbParade,    // three per-channel waveforms side by side
        Histogram,    // 256-bucket R/G/B overlay, additive channels
        Vectorscope,  // polar chroma plot: u = B - Y, v = R - Y
    };

    ScopesWidget() = default;
    ~ScopesWidget() override = default;

    // Feed the current monitor frame (packed BGRA8, stride in bytes).
    // Recomputes the scope bitmap when the source pointer/dims changed,
    // a mode change is pending, or >= ~100 ms elapsed since the last
    // compute. Pass nullptr to detach the source (frame was cleared).
    void SetFrame(const std::uint8_t* bgra, int width, int height,
                  int stride);

    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }
    // Force a recompute on the next SetFrame() call.
    void MarkDirty() { dirty_ = true; }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {160.0f, 100.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {256.0f, 190.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    XER::UI::Layout::Rect TabRect(int index) const;
    XER::UI::Layout::Rect ScopeAreaRect() const;
    void Recompute();
    void ComputeWaveform();
    void ComputeRgbParade();
    void ComputeHistogram();
    void ComputeVectorscope();

    Mode mode_ = Mode::Waveform;
    std::vector<std::uint8_t> bitmap_;    // fixed-size BGRA scope image
    std::vector<std::uint32_t> accum_;    // scratch intensity buffer
    std::vector<std::uint32_t> colmax_;   // scratch per-column maxima

    // Last fed source. Only dereferenced inside Recompute(), which
    // SetFrame() gates — the pointer is always refreshed there first.
    const std::uint8_t* src_ = nullptr;
    int src_w_ = 0;
    int src_h_ = 0;
    int src_stride_ = 0;
    bool has_source_ = false;
    bool dirty_ = true;

    // Throttle bookkeeping.
    std::uint64_t feed_count_ = 0;
    std::uint64_t computed_feed_ = 0;
    const std::uint8_t* computed_ptr_ = nullptr;
    int computed_w_ = 0;
    int computed_h_ = 0;
    std::chrono::steady_clock::time_point last_compute_{};
};

}  // namespace XER::VideoEditor::Ui
