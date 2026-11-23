#include "scopes_panel.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "shell_widgets.h"    // LinearBox
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/label.h"
#include "widgets/select.h"
#include "widgets/surface.h"

// windows.h may have re-defined DrawText before we got here.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;

constexpr float kHeaderH = 26.0f;

// Analysis sample cap — a strided pass touches at most ~64k px even
// on a 4K frame, keeping Refresh() under a millisecond or two and
// paint strictly non-blocking (analysis never runs inside paint).
constexpr std::size_t kMaxSamples = 65536;

// BT.601 luma of one packed BGRA pixel.
inline std::uint8_t Luma601(const std::uint8_t* px) {
    return static_cast<std::uint8_t>(
        std::clamp(0.114f * px[0] + 0.587f * px[1] +
                       0.299f * px[2],
                   0.0f, 255.0f));
}

const char* kModeNames[4] = {"Waveform", "Vectorscope", "Histogram",
                             "Parade"};

}  // namespace

std::shared_ptr<ScopesPanel> ScopesPanel::Create() {
    return std::shared_ptr<ScopesPanel>(new ScopesPanel());
}

ScopesPanel::ScopesPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void ScopesPanel::SetFrameSource(std::function<ScopeFrame()> source) {
    frame_source_ = std::move(source);
    analysis_valid_ = false;
    Invalidate();
}

void ScopesPanel::SetMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    syncing_ = true;
    if (mode_select_ != nullptr) {
        mode_select_->SetSelectedIndex(static_cast<int>(mode));
    }
    syncing_ = false;
    Invalidate();
}

void ScopesPanel::SetLayoutMode(LayoutMode layout) {
    if (layout_mode_ == layout) return;
    layout_mode_ = layout;
    syncing_ = true;
    if (layout_select_ != nullptr) {
        layout_select_->SetSelectedIndex(
            layout == LayoutMode::Quad ? 1 : 0);
    }
    syncing_ = false;
    Invalidate();
}

// ---- pull + lazy analysis ----------------------------------------------

bool ScopesPanel::Refresh() {
    ScopeFrame next = frame_source_ ? frame_source_() : ScopeFrame{};
    const bool changed = next.bgra.get() != frame_.bgra.get() ||
                         next.width != frame_.width ||
                         next.height != frame_.height;
    if (!changed) return false;
    frame_ = std::move(next);
    Analyze();
    Invalidate();
    return true;
}

void ScopesPanel::Analyze() {
    histogram_.fill(0);
    wave_min_.fill(255);
    wave_max_.fill(0);
    analysis_valid_ = false;
    if (frame_.bgra == nullptr || frame_.width <= 0 ||
        frame_.height <= 0) {
        return;
    }
    const std::size_t px_count =
        static_cast<std::size_t>(frame_.width) * frame_.height;
    if (frame_.bgra->size() / 4u < px_count) return;

    // Uniform stride so the pass stays bounded at ~64k samples.
    const std::size_t stride =
        px_count > kMaxSamples
            ? ((px_count + kMaxSamples - 1) / kMaxSamples)
            : 1;
    const int w = frame_.width;
    const auto* data = frame_.bgra->data();
    for (std::size_t i = 0; i < px_count; i += stride) {
        const std::uint8_t luma = Luma601(data + i * 4);
        ++histogram_[luma];
        // Column of the sampled pixel -> 0..255 envelope band.
        const int col = static_cast<int>(
            ((i % static_cast<std::size_t>(w)) * 256) /
            static_cast<std::size_t>(w));
        auto& lo = wave_min_[static_cast<std::size_t>(col)];
        auto& hi = wave_max_[static_cast<std::size_t>(col)];
        lo = std::min(lo, luma);
        hi = std::max(hi, luma);
    }
    analysis_valid_ = true;
}

// ---- controls ---------------------------------------------------------

void ScopesPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    mode_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{kModeNames, kModeNames + 4},
        static_cast<int>(mode_));
    mode_select_->SetOnChanged([this](int index) {
        if (!syncing_ && index >= 0 && index < 4) {
            mode_ = static_cast<Mode>(index);
            Invalidate();
        }
    });
    layout_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"Single", "Quad"},
        layout_mode_ == LayoutMode::Quad ? 1 : 0);
    layout_select_->SetOnChanged([this](int index) {
        if (!syncing_) {
            layout_mode_ =
                index == 1 ? LayoutMode::Quad : LayoutMode::Single;
            Invalidate();
        }
    });

    surface_ = std::make_shared<W::Surface>();
    surface_->SetPaintCallback(
        [this](Rendering::Renderer& r, const Layout::Rect& content,
               const XER::UI::Theme::Tokens& tokens) {
            PaintSurface(r, content, tokens);
        });

    AddChild(mode_select_);
    AddChild(layout_select_);
    AddChild(surface_);
}

// ---- paint --------------------------------------------------------------

void ScopesPanel::PaintSurface(Rendering::Renderer& renderer,
                               const Layout::Rect& content,
                               const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(content, tokens.background,
                      tokens.radius_small);
    renderer.StrokeRect(content, tokens.border, 1.0f,
                        tokens.radius_small);
    if (!analysis_valid_) {
        renderer.DrawText(
            frame_source_ ? "No frame" : "No frame source",
            content, {tokens.muted, Theme::kFontBody, 400});
        return;
    }
    if (layout_mode_ == LayoutMode::Quad) {
        const float hw = content.width / 2.0f;
        const float hh = content.height / 2.0f;
        const Layout::Rect cells[4] = {
            {content.x, content.y, hw, hh},
            {content.x + hw, content.y, hw, hh},
            {content.x, content.y + hh, hw, hh},
            {content.x + hw, content.y + hh, hw, hh}};
        for (int i = 0; i < 4; ++i) {
            DrawCell(renderer, tokens, cells[i],
                     static_cast<Mode>(i));
        }
    } else {
        DrawCell(renderer, tokens, content, mode_);
    }
}

void ScopesPanel::DrawCell(Rendering::Renderer& renderer,
                           const XER::UI::Theme::Tokens& tokens,
                           const Layout::Rect& cell, Mode mode) {
    const Layout::Rect inner = {cell.x + 2.0f, cell.y + 2.0f,
                                cell.width - 4.0f,
                                cell.height - 4.0f};
    if (inner.width < 8.0f || inner.height < 8.0f) return;
    renderer.StrokeRect(cell, tokens.border, 1.0f, 0.0f);
    renderer.DrawText(kModeNames[static_cast<int>(mode)],
                      {cell.x + 4.0f, cell.y + 2.0f,
                       cell.width - 8.0f, 14.0f},
                      {tokens.muted, Theme::kFontCaption, 500});
    const Layout::Rect plot = {inner.x, inner.y + 16.0f, inner.width,
                               inner.height - 16.0f};
    switch (mode) {
        case Mode::Waveform:
            DrawWaveform(renderer, tokens, plot);
            break;
        case Mode::Histogram:
            DrawHistogram(renderer, tokens, plot);
            break;
        case Mode::Vectorscope:
            DrawPlaceholder(renderer, tokens, plot,
                            "Vectorscope — not yet");
            break;
        case Mode::Parade:
            DrawPlaceholder(renderer, tokens, plot,
                            "Parade — not yet");
            break;
    }
}

void ScopesPanel::DrawWaveform(Rendering::Renderer& renderer,
                               const XER::UI::Theme::Tokens& tokens,
                               const Layout::Rect& plot) {
    // Graticule: 25/50/75% luma bands.
    for (int i = 1; i < 4; ++i) {
        const float gy =
            plot.y + plot.height * static_cast<float>(i) / 4.0f;
        renderer.DrawLine({plot.x, gy}, {plot.Right(), gy},
                          tokens.border, 1.0f);
    }
    const float col_w =
        std::max(1.0f, plot.width / 256.0f);
    const XER::UI::Theme::Color trace{
        tokens.track_audio.r, tokens.track_audio.g,
        tokens.track_audio.b, 0.75f};
    for (int c = 0; c < 256; ++c) {
        const auto lo = wave_min_[static_cast<std::size_t>(c)];
        const auto hi = wave_max_[static_cast<std::size_t>(c)];
        if (lo > hi) continue;   // no samples in this column
        const float x =
            plot.x + static_cast<float>(c) / 256.0f * plot.width;
        // y=0 at the TOP of the plot = luma 255 (broadcast
        // waveform convention).
        const float y_hi = plot.y + plot.height *
                                       (1.0f - hi / 255.0f);
        const float y_lo = plot.y + plot.height *
                                       (1.0f - lo / 255.0f);
        renderer.FillRect(
            {x, y_hi, col_w, std::max(1.0f, y_lo - y_hi)}, trace,
            0.0f);
    }
}

void ScopesPanel::DrawHistogram(Rendering::Renderer& renderer,
                                const XER::UI::Theme::Tokens& tokens,
                                const Layout::Rect& plot) {
    std::uint32_t peak = 1;
    for (const std::uint32_t bin : histogram_) {
        peak = std::max(peak, bin);
    }
    const float col_w =
        std::max(1.0f, plot.width / 256.0f);
    const float log_peak = std::log1p(static_cast<float>(peak));
    for (int i = 0; i < 256; ++i) {
        const float v =
            std::log1p(static_cast<float>(
                histogram_[static_cast<std::size_t>(i)])) /
            log_peak;
        if (v <= 0.0f) continue;
        const float h = v * plot.height;
        const float x =
            plot.x + static_cast<float>(i) / 256.0f * plot.width;
        // Left=dark, right=bright; bars colored by their luma level.
        const float lum = static_cast<float>(i) / 255.0f;
        renderer.FillRect(
            {x, plot.Bottom() - h, col_w, h},
            {lum, lum, lum, 0.85f}, 0.0f);
    }
    renderer.StrokeRect(plot, tokens.border, 1.0f, 0.0f);
}

void ScopesPanel::DrawPlaceholder(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens, const Layout::Rect& plot,
    const char* label) {
    // Honest stub: a framed cell + centered label — no fabricated
    // scope data (spec: unimplemented modes report "not yet").
    renderer.FillRect(plot, tokens.panel, 0.0f);
    renderer.StrokeRect(plot, tokens.border, 1.0f, 0.0f);
    renderer.DrawText(label, plot,
                      {tokens.muted, Theme::kFontBody, 400});
}

// ---- widget contract ----------------------------------------------------

void ScopesPanel::Render(Rendering::Renderer& renderer,
                         const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void ScopesPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float x = bounds.x + Theme::kSpaceSm;
    float w = bounds.width - Theme::kSpaceSm * 2.0f;
    const float y = bounds.y + Theme::kSpaceSm;
    const float sel_w = (w - Theme::kSpaceXs) / 2.0f;
    mode_select_->Layout({x, y, sel_w, kHeaderH}, tokens);
    layout_select_->Layout({x + sel_w + Theme::kSpaceXs, y, sel_w,
                            kHeaderH},
                           tokens);
    surface_->Layout(
        {x, y + kHeaderH + Theme::kSpaceXs, w,
         bounds.y + bounds.height - Theme::kSpaceSm - y - kHeaderH -
             Theme::kSpaceXs},
        tokens);
}

void ScopesPanel::Tick(double /*dt_seconds*/) {
    // Pull once per frame tick — analysis runs only when the frame
    // buffer changed (Refresh's identity check), so a still frame
    // costs one shared_ptr compare.
    if (frame_source_) Refresh();
}

}  // namespace XER::VideoEditor::Ui2
