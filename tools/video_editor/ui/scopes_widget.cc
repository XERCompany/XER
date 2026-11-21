#include "scopes_widget.h"

#include <algorithm>
#include <cmath>

#include "rendering/renderer.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/button_group.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;

// Fixed scope bitmap size — Render() stretches it into the overlay.
constexpr int kBmpW = 256;
constexpr int kBmpH = 160;
constexpr auto kMinComputeInterval = std::chrono::milliseconds(100);

// BT.601 integer luma.
int Luma(int r, int g, int b) { return (77 * r + 150 * g + 29 * b) >> 8; }

// Merges a pixel into the bitmap by per-channel max (additive-ish) so
// traces/graticule never darken existing content.
void PutMax(std::vector<std::uint8_t>& bmp, int x, int y, int b, int g,
            int r) {
    if (x < 0 || x >= kBmpW || y < 0 || y >= kBmpH) return;
    auto* p = bmp.data() + (static_cast<std::size_t>(y) * kBmpW + x) * 4;
    p[0] = static_cast<std::uint8_t>(std::max<int>(p[0], b));
    p[1] = static_cast<std::uint8_t>(std::max<int>(p[1], g));
    p[2] = static_cast<std::uint8_t>(std::max<int>(p[2], r));
    p[3] = 255;
}

// Tone-maps a column-histogram accumulation buffer into a tinted
// phosphor-style trace. `tints` holds one B/G/R scale triplet per
// output column (uniform for the waveform, per-section for parade).
struct Tint {
    float b, g, r;
};
void EmitTrace(const std::vector<std::uint32_t>& acc,
               const std::vector<std::uint32_t>& colmax,
               const std::vector<Tint>& tints,
               std::vector<std::uint8_t>& bmp) {
    for (int i = 0; i < kBmpW * kBmpH; ++i) {
        const std::uint32_t count = acc[static_cast<std::size_t>(i)];
        if (count == 0) continue;
        const std::uint32_t m = std::max(1u, colmax[i % kBmpW]);
        // Floor glow keeps sparse bins visible on high-res frames.
        const int v =
            std::min(255, 50 + static_cast<int>(count * 205 / m));
        const Tint& t = tints[static_cast<std::size_t>(i % kBmpW)];
        auto* p = bmp.data() + static_cast<std::size_t>(i) * 4;
        p[0] = static_cast<std::uint8_t>(std::min(255.0f, v * t.b));
        p[1] = static_cast<std::uint8_t>(std::min(255.0f, v * t.g));
        p[2] = static_cast<std::uint8_t>(std::min(255.0f, v * t.r));
        p[3] = 255;
    }
}

}  // namespace

void ScopesWidget::SetFrame(const std::uint8_t* bgra, int width,
                            int height, int stride) {
    src_ = bgra;
    src_w_ = width;
    src_h_ = height;
    src_stride_ = stride;
    has_source_ =
        bgra != nullptr && width > 0 && height > 0 && stride >= width * 4;
    ++feed_count_;
    if (!has_source_) {
        Invalidate();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool source_changed =
        bgra != computed_ptr_ || width != computed_w_ ||
        height != computed_h_;
    const bool due = now - last_compute_ >= kMinComputeInterval;
    if (dirty_ || source_changed ||
        (feed_count_ != computed_feed_ && due)) {
        Recompute();
    }
    Invalidate();
}

void ScopesWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    // Mode switch as a segmented strip (lab_panel convention): the
    // active scope keeps the Primary fill, the rest drop to
    // Secondary. Short labels — the strip overlays the monitor frame.
    mode_group_ = std::make_shared<Widgets::ButtonGroup>(
        Widgets::ButtonGroup::Orientation::Horizontal);
    wfm_btn_ = std::make_shared<Widgets::Button>(
        "WFM", Widgets::ButtonStyle::Primary);
    wfm_btn_->SetTooltip("Waveform — luma");
    wfm_btn_->SetOnClick([this] { SetMode(Mode::Waveform); });
    parade_btn_ = std::make_shared<Widgets::Button>(
        "PAR", Widgets::ButtonStyle::Secondary);
    parade_btn_->SetTooltip("RGB parade");
    parade_btn_->SetOnClick([this] { SetMode(Mode::RgbParade); });
    hist_btn_ = std::make_shared<Widgets::Button>(
        "HIST", Widgets::ButtonStyle::Secondary);
    hist_btn_->SetTooltip("Histogram — R/G/B overlay");
    hist_btn_->SetOnClick([this] { SetMode(Mode::Histogram); });
    vscope_btn_ = std::make_shared<Widgets::Button>(
        "VSC", Widgets::ButtonStyle::Secondary);
    vscope_btn_->SetTooltip("Vectorscope — chroma");
    vscope_btn_->SetOnClick([this] { SetMode(Mode::Vectorscope); });
    mode_group_->AddChild(wfm_btn_);
    mode_group_->AddChild(parade_btn_);
    mode_group_->AddChild(hist_btn_);
    mode_group_->AddChild(vscope_btn_);
    AddChild(mode_group_);
    SyncModeButtons();
}

void ScopesWidget::SyncModeButtons() {
    if (!children_attached_) return;
    namespace Widgets = XER::UI::Widgets;
    const auto style = [this](Mode mode) {
        return mode_ == mode ? Widgets::ButtonStyle::Primary
                             : Widgets::ButtonStyle::Secondary;
    };
    wfm_btn_->SetStyle(style(Mode::Waveform));
    parade_btn_->SetStyle(style(Mode::RgbParade));
    hist_btn_->SetStyle(style(Mode::Histogram));
    vscope_btn_->SetStyle(style(Mode::Vectorscope));
}

void ScopesWidget::SetMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    if (children_attached_) SyncModeButtons();
    dirty_ = true;  // next SetFrame() recomputes on a live pointer
    Invalidate();
}

void ScopesWidget::Recompute() {
    last_compute_ = std::chrono::steady_clock::now();
    computed_ptr_ = src_;
    computed_w_ = src_w_;
    computed_h_ = src_h_;
    computed_feed_ = feed_count_;
    dirty_ = false;
    bitmap_.assign(static_cast<std::size_t>(kBmpW) * kBmpH * 4, 0);
    if (!has_source_) return;
    switch (mode_) {
        case Mode::Waveform: ComputeWaveform(); break;
        case Mode::RgbParade: ComputeRgbParade(); break;
        case Mode::Histogram: ComputeHistogram(); break;
        case Mode::Vectorscope: ComputeVectorscope(); break;
    }
}

void ScopesWidget::ComputeWaveform() {
    accum_.assign(static_cast<std::size_t>(kBmpW) * kBmpH, 0);
    colmax_.assign(kBmpW, 0);
    for (int y = 0; y < src_h_; ++y) {
        const auto* row = src_ + static_cast<std::size_t>(y) * src_stride_;
        for (int x = 0; x < src_w_; ++x) {
            const auto* px = row + static_cast<std::size_t>(x) * 4;
            const int lum = Luma(px[2], px[1], px[0]);
            const int bx =
                static_cast<int>(static_cast<std::int64_t>(x) * kBmpW / src_w_);
            const int by = (kBmpH - 1) - lum * (kBmpH - 1) / 255;
            const std::uint32_t n =
                ++accum_[static_cast<std::size_t>(by) * kBmpW + bx];
            if (n > colmax_[bx]) colmax_[bx] = n;
        }
    }
    const std::vector<Tint> tints(kBmpW, {0.35f, 1.0f, 0.30f});  // green
    EmitTrace(accum_, colmax_, tints, bitmap_);
}

void ScopesWidget::ComputeRgbParade() {
    accum_.assign(static_cast<std::size_t>(kBmpW) * kBmpH, 0);
    colmax_.assign(kBmpW, 0);
    // Three 64-wide sections with gutters: x0 = 8 / 96 / 184.
    constexpr int kSecW = 64;
    constexpr int kX0[3] = {8, 96, 184};
    for (int y = 0; y < src_h_; ++y) {
        const auto* row = src_ + static_cast<std::size_t>(y) * src_stride_;
        for (int x = 0; x < src_w_; ++x) {
            const auto* px = row + static_cast<std::size_t>(x) * 4;
            const int bx =
                static_cast<int>(static_cast<std::int64_t>(x) * kSecW / src_w_);
            // Sections ordered R,G,B (BGRA byte lanes 2,1,0).
            for (int c = 0; c < 3; ++c) {
                const int by =
                    (kBmpH - 1) - px[2 - c] * (kBmpH - 1) / 255;
                const int col = kX0[c] + bx;
                const std::uint32_t n =
                    ++accum_[static_cast<std::size_t>(by) * kBmpW + col];
                if (n > colmax_[col]) colmax_[col] = n;
            }
        }
    }
    std::vector<Tint> tints(kBmpW, {0.0f, 0.0f, 0.0f});
    const Tint ch_tint[3] = {{0.22f, 0.22f, 1.0f},   // R section
                             {0.22f, 1.0f, 0.22f},   // G section
                             {1.0f, 0.22f, 0.22f}};  // B section
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < kSecW; ++i) tints[kX0[c] + i] = ch_tint[c];
    }
    EmitTrace(accum_, colmax_, tints, bitmap_);
    // Section separators.
    const int seps[] = {kX0[0] - 1, kX0[0] + kSecW, kX0[1] - 1,
                        kX0[1] + kSecW, kX0[2] - 1, kX0[2] + kSecW};
    for (int sx : seps) {
        for (int y = 0; y < kBmpH; ++y) PutMax(bitmap_, sx, y, 45, 45, 45);
    }
}

void ScopesWidget::ComputeHistogram() {
    // 256 buckets per channel, overlaid: each channel fills its own
    // byte lane in the BGRA bitmap so overlaps go additively bright.
    accum_.assign(256 * 3, 0);
    for (int y = 0; y < src_h_; ++y) {
        const auto* row = src_ + static_cast<std::size_t>(y) * src_stride_;
        for (int x = 0; x < src_w_; ++x) {
            const auto* px = row + static_cast<std::size_t>(x) * 4;
            ++accum_[px[0]];          // B
            ++accum_[256 + px[1]];    // G
            ++accum_[512 + px[2]];    // R
        }
    }
    std::uint32_t maxc = 1;
    for (std::uint32_t c : accum_) maxc = std::max(maxc, c);
    int heights[3][kBmpW];
    for (int c = 0; c < 3; ++c) {
        for (int x = 0; x < 256; ++x) {
            const double u =
                static_cast<double>(accum_[c * 256 + x]) / maxc;
            heights[c][x] =
                static_cast<int>(std::sqrt(u) * (kBmpH - 1));
        }
    }
    for (int x = 0; x < 256; ++x) {
        for (int depth = 0; depth < kBmpH; ++depth) {
            const int y = kBmpH - 1 - depth;
            auto* p = bitmap_.data() +
                      (static_cast<std::size_t>(y) * kBmpW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                if (depth >= heights[c][x]) continue;
                const int v = depth >= heights[c][x] - 2 ? 255 : 165;
                p[c] = static_cast<std::uint8_t>(std::max<int>(p[c], v));
            }
            p[3] = 255;
        }
    }
}

void ScopesWidget::ComputeVectorscope() {
    const float cx = kBmpW * 0.5f;
    const float cy = kBmpH * 0.5f;
    const float radius = 75.0f;
    const float kScale = 0.34f;  // |u|,|v| <= ~230 -> stays near circle

    // Graticule: skin-tone-adjacent 75% bar targets, circle, crosshair.
    const int kTargets[6][3] = {
        {191, 191, 0},   // Yl   {r,g,b}
        {191, 0, 0},     // R
        {191, 0, 191},   // Mg
        {0, 0, 191},     // B
        {0, 191, 191},   // Cy
        {0, 191, 0}};    // G
    for (const auto& t : kTargets) {
        const int lum = Luma(t[0], t[1], t[2]);
        const int tx = static_cast<int>(
            std::lround(cx + (t[2] - lum) * kScale));
        const int ty = static_cast<int>(
            std::lround(cy - (t[0] - lum) * kScale));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                PutMax(bitmap_, tx + dx, ty + dy, 70, 70, 70);
    }
    for (int i = 0; i < 1024; ++i) {
        const double a = i * 6.283185307179586 / 1024.0;
        PutMax(bitmap_,
               static_cast<int>(std::lround(cx + radius * std::cos(a))),
               static_cast<int>(std::lround(cy + radius * std::sin(a))),
               55, 60, 55);
    }
    for (int x = static_cast<int>(cx - radius);
         x <= static_cast<int>(cx + radius); ++x) {
        PutMax(bitmap_, x, static_cast<int>(cy), 45, 45, 45);
    }
    for (int y = static_cast<int>(cy - radius);
         y <= static_cast<int>(cy + radius); ++y) {
        PutMax(bitmap_, static_cast<int>(cx), y, 45, 45, 45);
    }

    // Chromaticity dots: u = B-Y (horizontal), v = R-Y (vertical up).
    accum_.assign(static_cast<std::size_t>(kBmpW) * kBmpH, 0);
    for (int y = 0; y < src_h_; ++y) {
        const auto* row = src_ + static_cast<std::size_t>(y) * src_stride_;
        for (int x = 0; x < src_w_; ++x) {
            const auto* px = row + static_cast<std::size_t>(x) * 4;
            const int lum = Luma(px[2], px[1], px[0]);
            const int u = px[0] - lum;
            const int v = px[2] - lum;
            const int sx =
                static_cast<int>(std::lround(cx + u * kScale));
            const int sy =
                static_cast<int>(std::lround(cy - v * kScale));
            if (sx >= 0 && sx < kBmpW && sy >= 0 && sy < kBmpH) {
                ++accum_[static_cast<std::size_t>(sy) * kBmpW + sx];
            }
        }
    }
    std::uint32_t maxc = 1;
    for (std::uint32_t c : accum_) maxc = std::max(maxc, c);
    const double inv_log = 1.0 / std::log(1.0 + maxc);
    for (int i = 0; i < kBmpW * kBmpH; ++i) {
        const std::uint32_t count = accum_[static_cast<std::size_t>(i)];
        if (count == 0) continue;
        const int v = std::min(
            255, static_cast<int>(std::log(1.0 + count) * inv_log * 255));
        PutMax(bitmap_, i % kBmpW, i / kBmpW,
               static_cast<int>(v * 0.35), v, static_cast<int>(v * 0.30));
    }
}

// ---- geometry / render / input ----

Layout::Rect ScopesWidget::ScopeAreaRect() const {
    const auto b = GetBounds();
    return {b.x + 4.0f, b.y + 30.0f,
            std::max(0.0f, b.width - 8.0f),
            std::max(0.0f, b.height - 34.0f)};
}

void ScopesWidget::Render(Rendering::Renderer& renderer,
                          const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    if (bounds.width < 40.0f || bounds.height < 30.0f) return;

    const auto area = ScopeAreaRect();
    renderer.FillRect(area, {0.0f, 0.0f, 0.0f, 0.45f},
                      tokens.radius_small);
    if (!bitmap_.empty()) {
        renderer.DrawFrameBgra(bitmap_.data(), kBmpW, kBmpH, area,
                               0.95f);
    }
    renderer.StrokeRect(area, VeTheme::WithAlpha(tokens.border, 0.8f),
                        1.0f, 0.0f);

    // Off-tree host: the monitor renders this widget directly, so the
    // strip's children are painted by hand — a WidgetTree would do
    // this walk for a child it owned.
    mode_group_->Render(renderer, tokens);
    for (const auto& child : mode_group_->GetChildren()) {
        child->Render(renderer, tokens);
    }
}

void ScopesWidget::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    // Segmented strip across the top; the scope bitmap sits below it.
    mode_group_->Layout({bounds.x + 4.0f, bounds.y + 4.0f,
                         std::max(0.0f, bounds.width - 8.0f), 22.0f},
                        CachedLayoutTokens());
}

bool ScopesWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    EnsureChildren();
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        // Manual child dispatch — same off-tree caveat as Render().
        // Buttons fire their OnClick on a left Click, which is the
        // only event type the monitor forwards into the overlay.
        for (const auto& child : mode_group_->GetChildren()) {
            if (child->IsVisible() &&
                child->GetBounds().Contains(event.position)) {
                child->HandleEvent(event);
                return true;
            }
        }
        return GetBounds().Contains(event.position);
    }
    return false;
}

}  // namespace XER::VideoEditor::Ui
