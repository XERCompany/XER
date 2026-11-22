// ScopesPanel — the M3 "Scopes" right-dock tab of the ui2 rebuild
// (spec §6): pull-based video scopes over the program monitor's
// current BGRA frame:
//
//   [ mode Select (Waveform/Vectorscope/Histogram/Parade) ] [ Single|Quad ]
//   +------------------------------------------------------+
//   | ScopeSurface — kit Surface whose paint callback only  |
//   | blits the last completed analysis (never computes)    |
//   +------------------------------------------------------+
//
// Frame seam: SetFrameSource(fn) returns a ScopeFrame (shared BGRA
// buffer, no copy). VeApp binds it to the program monitor's
// MonitorSurface::CurrentFrame; headless checks inject a synthetic
// frame. Refresh() — called from the shell's Tick and directly by
// smoke — pulls the frame and runs analysis ONLY when the buffer
// identity changed, so paint is never blocked and unchanged frames
// cost a pointer compare.
//
// Implemented scope math (honest coverage):
//   * Waveform/Y — 256-column luma min/max envelope (BT.601 luma,
//     strided sampling capped ~64k px), drawn as a filled envelope.
//   * Histogram — 256-bin luma histogram on the same samples, drawn
//     as a normalized bar chart.
//   * Vectorscope + Parade — NOT implemented: the cells draw an
//     explicit "not yet" placeholder behind the same interface —
//     no fabricated data.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Select;
class Surface;
class Widget;
}

namespace XER::VideoEditor::Ui2 {

// One BGRA8 frame snapshot handed to the scope analysis — the same
// shape as MonitorSurface::Frame (shared so pull = pointer copy).
struct ScopeFrame {
    std::shared_ptr<const std::vector<std::uint8_t>> bgra;
    int width = 0;
    int height = 0;
};

class ScopesPanel final : public XER::UI::Widgets::Widget {
public:
    // Order matches the ColorPanel scope chips (WFM/VEC/HST/PAR).
    enum class Mode {
        Waveform = 0,
        Vectorscope = 1,
        Histogram = 2,
        Parade = 3,
    };
    enum class LayoutMode { Single, Quad };

    static std::shared_ptr<ScopesPanel> Create();

    // Pull seam — returns the current frame or an empty ScopeFrame.
    // Called from Refresh() (Tick-driven); may be nullptr.
    void SetFrameSource(std::function<ScopeFrame()> source);
    bool HasFrameSource() const {
        return static_cast<bool>(frame_source_);
    }
    const ScopeFrame& Frame() const { return frame_; }

    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }
    void SetLayoutMode(LayoutMode layout);
    LayoutMode GetLayoutMode() const { return layout_mode_; }
    // Whether the mode draws real analyzed data (vs the honest
    // "not yet" placeholder cell).
    static bool ModeImplemented(Mode mode) {
        return mode == Mode::Waveform || mode == Mode::Histogram;
    }

    // Pull + analyze-on-change entry: safe to call every Tick —
    // analysis runs only when the pulled buffer identity changed.
    // Returns true when new analysis ran (smoke asserts on it).
    bool Refresh();

    // ---- analysis results (valid after Refresh on a new frame) ----
    const std::array<std::uint32_t, 256>& LumaHistogram() const {
        return histogram_;
    }
    // Per-column luma envelope for the waveform (256 columns, values
    // 0..255; a column with no samples reports min>max).
    const std::array<std::uint8_t, 256>& WaveformMax() const {
        return wave_max_;
    }
    const std::array<std::uint8_t, 256>& WaveformMin() const {
        return wave_min_;
    }
    bool AnalysisValid() const { return analysis_valid_; }

    // ---- widget contract ----
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {240.0f, 180.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {340.0f, 340.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit ScopesPanel();

    void EnsureChildren();
    void PaintSurface(XER::UI::Rendering::Renderer& renderer,
                      const XER::UI::Layout::Rect& content,
                      const XER::UI::Theme::Tokens& tokens);
    void DrawCell(XER::UI::Rendering::Renderer& renderer,
                  const XER::UI::Theme::Tokens& tokens,
                  const XER::UI::Layout::Rect& cell, Mode mode);
    void DrawWaveform(XER::UI::Rendering::Renderer& renderer,
                      const XER::UI::Theme::Tokens& tokens,
                      const XER::UI::Layout::Rect& cell);
    void DrawHistogram(XER::UI::Rendering::Renderer& renderer,
                       const XER::UI::Theme::Tokens& tokens,
                       const XER::UI::Layout::Rect& cell);
    void DrawPlaceholder(XER::UI::Rendering::Renderer& renderer,
                         const XER::UI::Theme::Tokens& tokens,
                         const XER::UI::Layout::Rect& cell,
                         const char* label);
    // Strided luma pass — fills histogram_ + wave_min_/wave_max_.
    // Runs from Refresh() on frame change, never from paint.
    void Analyze();

    std::function<ScopeFrame()> frame_source_;
    ScopeFrame frame_;
    Mode mode_ = Mode::Waveform;
    LayoutMode layout_mode_ = LayoutMode::Single;
    bool children_attached_ = false;
    bool syncing_ = false;

    bool analysis_valid_ = false;
    std::array<std::uint32_t, 256> histogram_{};
    std::array<std::uint8_t, 256> wave_min_{};
    std::array<std::uint8_t, 256> wave_max_{};

    std::shared_ptr<XER::UI::Widgets::Select> mode_select_;
    std::shared_ptr<XER::UI::Widgets::Select> layout_select_;
    std::shared_ptr<XER::UI::Widgets::Surface> surface_;
};

}  // namespace XER::VideoEditor::Ui2
