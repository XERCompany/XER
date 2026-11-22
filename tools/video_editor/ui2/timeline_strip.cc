// TimelineStrip — widget-composed tool row above the TimelineSurface.
// Layout: [tool group V..Z] [SNAP] [LINK] [zoom slider] [fit]
// [height slider] across a 32 DIP row; the surface gets the rest.
// The zoom slider is log-mapped (8..600 px/s — the wheel zoom's
// clamp range) and re-syncs from the surface every Tick while it is
// not being dragged, so wheel zooms and ZoomAt/ZoomToRange calls
// stay visible on the track.

#include "timeline_strip.h"

#include <algorithm>
#include <cmath>

#include "rendering/renderer.h"
#include "ve_theme.h"

namespace XER::VideoEditor::Ui2 {

namespace {
namespace Widgets = XER::UI::Widgets;
namespace Theme = XER::VideoEditor::Ui2::Theme;
using XER::UI::Theme::Tokens;
namespace Layout = XER::UI::Layout;

constexpr double kMinZoom = 8.0;    // surface wheel-zoom clamp range
constexpr double kMaxZoom = 600.0;
constexpr float kMinTrackHeight = 36.0f;
constexpr float kMaxTrackHeight = 140.0f;

// Tool letters in strip order — same order the ShortcutRouter binds.
const char* kToolLetters[] = {"V", "C", "B", "N", "Y",
                              "U", "R", "H", "Z"};
const char* kToolNames[] = {
    "Selection (V)",    "Razor (C)",       "Ripple edit (B)",
    "Rolling edit (N)", "Slip (Y)",        "Slide (U)",
    "Rate stretch (R)", "Hand (H)",        "Zoom (Z)",
};
static_assert(std::size(kToolLetters) == kToolCount);

std::shared_ptr<Widgets::Button> MakeChip(std::string text,
                                          std::string tooltip) {
    auto b = std::make_shared<Widgets::Button>(std::move(text));
    b->SetStyle(Widgets::ButtonStyle::Ghost);
    b->SetSize(Widgets::ButtonSize::Small);
    b->SetTooltip(std::move(tooltip));
    b->SetCheckable(true);
    return b;
}

}  // namespace

std::shared_ptr<TimelineStrip> TimelineStrip::Create() {
    auto t = std::shared_ptr<TimelineStrip>(new TimelineStrip());
    t->SetFocusable(false);

    // --- tool button group ---
    t->tool_group_ =
        std::make_shared<Widgets::ButtonGroup>(
            Widgets::ButtonGroup::Orientation::Horizontal);
    TimelineStrip* self = t.get();   // raw — shared_ptr would cycle
    for (int i = 0; i < kToolCount; ++i) {
        auto b = MakeChip(kToolLetters[i], kToolNames[i]);
        const int index = i;
        b->SetOnCheckedChanged([self, index](bool checked) {
            if (!checked || self->updating_checks_) return;
            self->surface_->SetToolIndex(index);
        });
        t->tool_buttons_.push_back(b);
        t->tool_group_->AddChild(b);
    }
    t->tool_buttons_[0]->SetChecked(true);
    t->AddChild(t->tool_group_);

    // --- snap toggle ---
    t->snap_button_ = MakeChip("SNAP", "Snap to edges/markers (S)");
    t->snap_button_->SetChecked(true);
    t->snap_button_->SetOnCheckedChanged([self](bool checked) {
        self->snap_enabled_ = checked;
        self->surface_->SetSnapEnabled(checked);
        if (self->on_snap_toggled_ != nullptr) {
            self->on_snap_toggled_(checked);
        }
    });
    t->AddChild(t->snap_button_);

    // --- linkage toggle (Premiere "Linked Selection") ---
    t->link_button_ = MakeChip("LINK", "Linked selection");
    t->link_button_->SetChecked(true);
    t->link_button_->SetOnCheckedChanged([self](bool checked) {
        self->link_enabled_ = checked;
        self->surface_->SetLinkEditsEnabled(checked);
    });
    t->AddChild(t->link_button_);

    // --- zoom slider (log-mapped px/second) ---
    t->zoom_slider_ = std::make_shared<Widgets::Slider>(0.0f, 1.0f, 0.5f);
    t->zoom_slider_->SetTooltip("Zoom");
    t->zoom_slider_->SetOnValueChanged([self](float v) {
        self->surface_->SetZoom(self->SliderToZoom(v));
    });
    t->AddChild(t->zoom_slider_);

    // --- fit button ---
    t->fit_button_ = std::make_shared<Widgets::Button>("fit");
    t->fit_button_->SetStyle(Widgets::ButtonStyle::Ghost);
    t->fit_button_->SetSize(Widgets::ButtonSize::Small);
    t->fit_button_->SetTooltip("Zoom to fit project");
    t->fit_button_->SetOnClick(
        [self] { self->surface_->ZoomToFit(); });
    t->AddChild(t->fit_button_);

    // --- track-height slider ---
    t->height_slider_ = std::make_shared<Widgets::Slider>(
        kMinTrackHeight, kMaxTrackHeight, 64.0f);
    t->height_slider_->SetTooltip("Track height");
    t->height_slider_->SetOnValueChanged([self](float v) {
        self->surface_->SetTrackHeight(v);
    });
    t->AddChild(t->height_slider_);

    // --- the surface ---
    t->surface_ = std::make_shared<TimelineSurface>();
    t->AddChild(t->surface_);

    // Surface-side tool changes (shortcuts, the corner razor
    // button) sync the strip's check states and re-emit to the
    // shell in the placeholder's int-index form.
    t->surface_->SetOnToolChanged([self](ToolId id) {
        self->SyncToolChecks(id);
        if (self->on_tool_changed_ != nullptr) {
            self->on_tool_changed_(ToolIndexFor(id));
        }
    });
    return t;
}

void TimelineStrip::SyncToolChecks(ToolId active) {
    updating_checks_ = true;
    for (int i = 0; i < kToolCount; ++i) {
        tool_buttons_[static_cast<std::size_t>(i)]->SetChecked(
            ToolIdForIndex(i) == active);
    }
    updating_checks_ = false;
}

void TimelineStrip::SetTool(int index) {
    if (surface_ != nullptr) surface_->SetToolIndex(index);
}

int TimelineStrip::ActiveTool() const {
    return surface_ != nullptr
               ? ToolIndexFor(surface_->ActiveTool())
               : 0;
}

void TimelineStrip::SetOnToolChanged(std::function<void(int)> cb) {
    on_tool_changed_ = std::move(cb);
}

void TimelineStrip::SetOnSnapToggled(std::function<void(bool)> cb) {
    on_snap_toggled_ = std::move(cb);
}

void TimelineStrip::SetSnapEnabled(bool enabled) {
    snap_enabled_ = enabled;
    if (snap_button_ != nullptr) snap_button_->SetChecked(enabled);
    if (surface_ != nullptr) surface_->SetSnapEnabled(enabled);
    Invalidate();
}

bool TimelineStrip::SnapEnabled() const { return snap_enabled_; }

void TimelineStrip::SetLinkageEnabled(bool enabled) {
    link_enabled_ = enabled;
    if (link_button_ != nullptr) link_button_->SetChecked(enabled);
    if (surface_ != nullptr) surface_->SetLinkEditsEnabled(enabled);
}

bool TimelineStrip::LinkageEnabled() const { return link_enabled_; }

float TimelineStrip::ZoomToSlider(double px_per_second) const {
    const double clamped =
        std::clamp(px_per_second, kMinZoom, kMaxZoom);
    return static_cast<float>(
        std::log(clamped / kMinZoom) / std::log(kMaxZoom / kMinZoom));
}

double TimelineStrip::SliderToZoom(float value) const {
    const double v = std::clamp(static_cast<double>(value), 0.0, 1.0);
    return kMinZoom * std::pow(kMaxZoom / kMinZoom, v);
}

void TimelineStrip::Render(XER::UI::Rendering::Renderer& renderer,
                           const Tokens& tokens) {
    const Layout::Rect b = GetBounds();
    renderer.FillRect(b, tokens.panel, 0.0f);
    renderer.DrawLine({b.x, b.y + tool_strip_height_ - 0.5f},
                      {b.x + b.width,
                       b.y + tool_strip_height_ - 0.5f},
                      tokens.border, 1.0f);
}

void TimelineStrip::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const float row_y = bounds.y + 2.0f;
    const float row_h = tool_strip_height_ - 4.0f;
    float x = bounds.x + Theme::kSpaceXs;

    const auto place = [&](const std::shared_ptr<Widgets::Widget>& w,
                           float width) {
        const Layout::Rect r{x, row_y, width, row_h};
        w->Layout(r, CachedLayoutTokens());
        w->SetBounds(r);
        x += width + Theme::kSpaceXs;
    };

    // 9 tool buttons at ~24 DIP each inside the joined group.
    place(tool_group_, 9.0f * 24.0f + 2.0f);
    x += Theme::kSpaceXs;
    place(snap_button_, 52.0f);
    place(link_button_, 52.0f);
    x += Theme::kSpaceXs;
    // Zoom slider takes a flexible share of the remaining width;
    // the fit button and height slider keep fixed slots at the
    // right edge.
    const float fit_w = 34.0f;
    const float height_w = 84.0f;
    const float right_pad = Theme::kSpaceXs;
    const float remaining =
        bounds.x + bounds.width - x - fit_w - height_w -
        right_pad - Theme::kSpaceXs * 3.0f;
    place(zoom_slider_, std::clamp(remaining, 80.0f, 220.0f));
    place(fit_button_, fit_w);
    place(height_slider_, height_w);

    if (surface_ != nullptr) {
        const Layout::Rect body{
            bounds.x, bounds.y + tool_strip_height_, bounds.width,
            std::max(0.0f, bounds.height - tool_strip_height_)};
        surface_->Layout(body, CachedLayoutTokens());
        surface_->SetBounds(body);
    }
}

void TimelineStrip::Tick(double dt_seconds) {
    (void)dt_seconds;
    // Sync the zoom slider with surface-side zoom changes (wheel,
    // ZoomAt/ZoomToRange, ZoomToFit) while the user isn't holding
    // the thumb.
    if (zoom_slider_ != nullptr && surface_ != nullptr &&
        !zoom_slider_->IsDragging()) {
        const float want = ZoomToSlider(surface_->PxPerSecond());
        if (std::fabs(want - zoom_slider_->GetValue()) > 0.002f) {
            zoom_slider_->SetValue(want);
        }
    }
}

Layout::Size TimelineStrip::GetMinSize() const {
    return {480.0f, tool_strip_height_ + 120.0f};
}

Layout::Size TimelineStrip::GetPreferredSize() const {
    return {960.0f, tool_strip_height_ + 260.0f};
}

Layout::Size TimelineStrip::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

}  // namespace XER::VideoEditor::Ui2
