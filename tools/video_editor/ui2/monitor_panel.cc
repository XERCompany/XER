#include "monitor_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "lab/object_ops.h"           // Lab::IntPoint (complete type for vector)
#include "rendering/renderer.h"
#include "timecode.h"               // FormatTimecode — mono readout
#include "ui/icon_helpers.h"        // VeIcons::InstallButtonIconResolver
#include "ve_theme.h"
#include "widgets/badge.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/level_meter.h"
#include "widgets/select.h"
#include "widgets/slider.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
namespace W = XER::UI::Widgets;
using XER::UI::Theme::Color;

// ---- geometry constants (4px grid, pro-NLE density) ----
constexpr float kHeaderHeight = 30.0f;
constexpr float kTransportHeight = 34.0f;
constexpr float kButtonW = 30.0f;
constexpr float kPlayW = 34.0f;
constexpr float kGap = 4.0f;
constexpr float kMetersWidth = 16.0f;
constexpr float kTimecodeWidth = 132.0f;
constexpr float kQualityWidth = 76.0f;
constexpr float kZoomWidth = 72.0f;
constexpr float kBadgeH = 18.0f;

// Centered transport strip order/widths (old-monitor parity + a
// dedicated Stop the old row folded into the play toggle).
constexpr std::array<MonitorPanel::ChromeId, 6> kCenterStrip = {
    MonitorPanel::ChromeId::ToStart,
    MonitorPanel::ChromeId::PrevFrame,
    MonitorPanel::ChromeId::Play,
    MonitorPanel::ChromeId::Stop,
    MonitorPanel::ChromeId::NextFrame,
    MonitorPanel::ChromeId::ToEnd,
};
constexpr std::array<float, 6> kCenterW = {
    kButtonW, kButtonW, kPlayW, kButtonW, kButtonW, kButtonW,
};

// Right cluster order (left→right). A PREFIX survives when the row
// narrows — tail entries drop first (old RightClusterKept rule).
constexpr std::array<MonitorPanel::ChromeId, 10> kRightCluster = {
    MonitorPanel::ChromeId::MarkIn,
    MonitorPanel::ChromeId::MarkOut,
    MonitorPanel::ChromeId::Insert,
    MonitorPanel::ChromeId::Overwrite,
    MonitorPanel::ChromeId::SafeMargins,
    MonitorPanel::ChromeId::Grid,
    MonitorPanel::ChromeId::Loop,
    MonitorPanel::ChromeId::QualitySelect,
    MonitorPanel::ChromeId::ZoomSlider,
    MonitorPanel::ChromeId::Timecode,  // sentinel — never placed right
};

// Per-scope swap of the edit-cluster pair inside kRightCluster:
// program monitors carry Lift/Extract where source monitors carry
// Insert/Overwrite.
constexpr std::array<MonitorPanel::ChromeId, 10> kRightClusterProgram = {
    MonitorPanel::ChromeId::MarkIn,
    MonitorPanel::ChromeId::MarkOut,
    MonitorPanel::ChromeId::Lift,
    MonitorPanel::ChromeId::Extract,
    MonitorPanel::ChromeId::SafeMargins,
    MonitorPanel::ChromeId::Grid,
    MonitorPanel::ChromeId::Loop,
    MonitorPanel::ChromeId::QualitySelect,
    MonitorPanel::ChromeId::ZoomSlider,
    MonitorPanel::ChromeId::Timecode,
};

float ChromeWidth(MonitorPanel::ChromeId id) {
    using Id = MonitorPanel::ChromeId;
    switch (id) {
        case Id::MarkIn:        return 34.0f;
        case Id::MarkOut:       return 40.0f;
        case Id::Insert:        return 36.0f;
        case Id::Overwrite:     return 40.0f;
        case Id::Lift:          return 40.0f;
        case Id::Extract:       return 38.0f;
        case Id::SafeMargins:
        case Id::Grid:
        case Id::Loop:          return kButtonW;
        case Id::QualitySelect: return kQualityWidth;
        case Id::ZoomSlider:    return kZoomWidth;
        default:                return kButtonW;
    }
}

float ToDb(float linear_peak) {
    if (linear_peak <= 0.0f || !std::isfinite(linear_peak)) {
        return -60.0f;
    }
    return std::max(-60.0f, 20.0f * std::log10(linear_peak));
}

// Mono timecode readout — the kit Label can't express monospace +
// the timecode token, so the readout is a tiny local widget (same
// paint the old monitor's DrawTransport did inline).
class TimecodeLabel final : public W::Widget {
public:
    void SetText(std::string text) {
        if (text_ == text) return;
        text_ = std::move(text);
        Invalidate();
    }
    const std::string& GetText() const { return text_; }
    std::string GetDebugText() const override { return text_; }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        const Rendering::TextStyle style{
            tokens.timecode, tokens.font_title, 700,
            /*monospace=*/true};
        const auto size = renderer.MeasureText(text_, style);
        const Layout::Rect rect{
            b.x + 2.0f,
            b.y + std::max(0.0f, (b.height - size.height) * 0.5f),
            b.width - 4.0f, size.height};
        renderer.DrawText(text_, rect, style);
    }
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }
    bool HandleEvent(const W::Event&) override { return false; }
    void Tick(double) override {}
    Layout::Size GetMinSize() const override { return {90.0f, 20.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {kTimecodeWidth, 28.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    std::string text_ = "00:00:00:00";
};

}  // namespace

std::shared_ptr<MonitorPanel> MonitorPanel::Create(Kind kind) {
    return std::shared_ptr<MonitorPanel>(new MonitorPanel(kind));
}

MonitorPanel::MonitorPanel(Kind kind) : kind_(kind) {
    BuildChildren();
}

void MonitorPanel::BuildChildren() {
    Ui::VeIcons::InstallButtonIconResolver();   // idempotent

    surface_ = MonitorSurface::Create(
        kind_ == Kind::Program ? MonitorSurface::Id::Program
                               : MonitorSurface::Id::Source);
    // Surface hooks -> panel hooks (last-active-viewer + transport).
    surface_->SetOnActivate([this] { Activate(); });
    surface_->SetOnTimeChanged([this](double t) {
        Activate();
        if (hooks_.on_seek) hooks_.on_seek(t);
    });
    surface_->SetOnTogglePlay([this] {
        Activate();
        if (hooks_.on_play_toggle) hooks_.on_play_toggle();
    });
    surface_->SetOnClickPoint([this](float nx, float ny) {
        Activate();
        if (hooks_.on_click_point) hooks_.on_click_point(nx, ny);
    });
    surface_->SetOnStatus([this](const std::string& s, bool err) {
        if (hooks_.on_status) hooks_.on_status(s, err);
    });
    surface_->SetOnZoomChanged([this](float zoom) {
        if (syncing_zoom_ || zoom_slider_ == nullptr) return;
        syncing_zoom_ = true;
        zoom_slider_->SetValue(std::clamp(
            std::log2(std::max(zoom, 0.25f)) / 2.0f, -1.0f, 1.0f));
        syncing_zoom_ = false;
    });
    AddChild(surface_);

    meter_ = std::make_shared<W::LevelMeter>();
    meter_->SetChannelCount(2);
    meter_->SetOrientation(W::LevelMeter::Orientation::Vertical);
    meter_->SetDbRange(-60.0f, 0.0f);
    meter_->SetBallistics(W::LevelMeter::Ballistics::Peak);
    meter_->SetPeakHold(1.6f);
    meter_->SetTooltip("Audio levels (dBFS)");
    AddChild(meter_);

    title_label_ = std::make_shared<W::Label>(
        kind_ == Kind::Program ? "PROGRAM" : "SOURCE");
    AddChild(title_label_);

    proxy_badge_ = std::make_shared<W::Badge>("",
                                              W::BadgeVariant::Accent);
    proxy_badge_->SetVisible(false);
    AddChild(proxy_badge_);
    quality_badge_ = std::make_shared<W::Badge>(
        "Full", W::BadgeVariant::Secondary);
    quality_badge_->SetTooltip("Preview quality");
    AddChild(quality_badge_);

    timecode_ = std::make_shared<TimecodeLabel>();
    AddChild(timecode_);

    const auto make_btn = [this](const char* text, const char* icon,
                                 const char* tooltip) {
        auto b = std::make_shared<W::Button>(
            text != nullptr ? text : "", W::ButtonStyle::Ghost);
        b->SetSize(W::ButtonSize::Small);
        // JKL/I-O keys stay app-level — the row's buttons take
        // pointer input only, never keyboard focus (old parity).
        b->SetFocusable(false);
        if (icon != nullptr) b->SetIcon(icon);
        if (tooltip != nullptr) b->SetTooltip(tooltip);
        AddChild(b);
        return b;
    };
    const auto fire = [this](std::function<void()>* fn) {
        return [this, fn] {
            Activate();
            if (*fn) (*fn)();
        };
    };

    to_start_btn_ = make_btn("|‹", nullptr, "Go to start (Home)");
    to_start_btn_->SetOnClick(fire(&hooks_.on_to_start));
    prev_btn_ = make_btn(nullptr, "transport.step_back",
                         "Previous frame (←)");
    prev_btn_->SetOnClick([this] {
        Activate();
        if (hooks_.on_frame_step) hooks_.on_frame_step(-1);
    });
    play_btn_ = make_btn(nullptr, "transport.play",
                         "Play / pause (Space)");
    play_btn_->SetOnClick(fire(&hooks_.on_play_toggle));
    stop_btn_ = make_btn(nullptr, "transport.stop",
                         "Stop — pause + reset shuttle (K)");
    stop_btn_->SetOnClick(fire(&hooks_.on_stop));
    next_btn_ = make_btn(nullptr, "transport.step_forward",
                         "Next frame (→)");
    next_btn_->SetOnClick([this] {
        Activate();
        if (hooks_.on_frame_step) hooks_.on_frame_step(1);
    });
    to_end_btn_ = make_btn("›|", nullptr, "Go to end (End)");
    to_end_btn_->SetOnClick(fire(&hooks_.on_to_end));

    mark_in_btn_ = make_btn("IN", nullptr,
                            kind_ == Kind::Source
                                ? "Mark in (I)"
                                : "Set work-area in (I)");
    mark_in_btn_->SetOnClick(fire(&hooks_.on_mark_in));
    mark_out_btn_ = make_btn("OUT", nullptr,
                             kind_ == Kind::Source
                                 ? "Mark out (O)"
                                 : "Set work-area out (O)");
    mark_out_btn_->SetOnClick(fire(&hooks_.on_mark_out));

    if (kind_ == Kind::Source) {
        insert_btn_ = make_btn("INS", nullptr, "Insert edit (,)");
        insert_btn_->SetOnClick(fire(&hooks_.on_insert));
        overwrite_btn_ =
            make_btn("OVR", nullptr, "Overwrite edit (.)");
        overwrite_btn_->SetOnClick(fire(&hooks_.on_overwrite));
    } else {
        lift_btn_ = make_btn("LIFT", nullptr,
                             "Lift range (leaves gap)");
        lift_btn_->SetOnClick(fire(&hooks_.on_lift));
        extract_btn_ = make_btn("EXT", nullptr,
                                "Extract range (closes gap)");
        extract_btn_->SetOnClick(fire(&hooks_.on_extract));
    }

    safe_btn_ = make_btn(nullptr, "transport.safe",
                         "Safe margins overlay");
    safe_btn_->SetCheckable(true);
    safe_btn_->SetOnCheckedChanged([this](bool on) {
        Activate();
        if (hooks_.on_safe_toggled) hooks_.on_safe_toggled(on);
    });
    grid_btn_ = make_btn(nullptr, "transport.grid",
                         "Rule-of-thirds grid");
    grid_btn_->SetCheckable(true);
    grid_btn_->SetOnCheckedChanged([this](bool on) {
        Activate();
        if (hooks_.on_grid_toggled) hooks_.on_grid_toggled(on);
    });
    loop_btn_ = make_btn(nullptr, "transport.loop", "Loop playback");
    loop_btn_->SetCheckable(true);
    loop_btn_->SetOnCheckedChanged([this](bool on) {
        Activate();
        if (hooks_.on_loop_toggled) hooks_.on_loop_toggled(on);
    });

    quality_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"Full", "1/2", "1/4"}, 0);
    quality_select_->SetFocusable(false);
    quality_select_->SetTooltip("Preview quality");
    quality_select_->SetOnChanged([this](int index) {
        if (syncing_quality_) return;
        static const float kScales[] = {1.0f, 0.5f, 0.25f};
        if (index < 0 || index > 2) return;
        Activate();
        quality_scale_ = kScales[index];
        if (hooks_.on_quality) hooks_.on_quality(kScales[index]);
        // The badge mirrors the pick (old widget parity).
        static const char* kTags[] = {"Full", "1/2", "1/4"};
        quality_badge_->SetText(kTags[index]);
    });
    AddChild(quality_select_);

    // Zoom slider: [-1,+1] slider domain maps log2 to 0.25x..4x over
    // the letterboxed fit; wheel zoom on the surface reports back
    // through SetOnZoomChanged (clamped display, no feedback loop).
    zoom_slider_ = std::make_shared<W::Slider>(-1.0f, 1.0f, 0.0f);
    zoom_slider_->SetTooltip("Monitor zoom (wheel / drag to pan)");
    zoom_slider_->SetOnValueChanged([this](float v) {
        if (syncing_zoom_) return;
        Activate();
        syncing_zoom_ = true;
        surface_->SetZoom(std::pow(2.0f, v * 2.0f));
        syncing_zoom_ = false;
    });
    AddChild(zoom_slider_);
}

void MonitorPanel::Activate() {
    if (hooks_.on_activate) hooks_.on_activate();
}

void MonitorPanel::SetHooks(Hooks hooks) { hooks_ = std::move(hooks); }

XER::UI::Widgets::Widget* MonitorPanel::Chrome(ChromeId id) const {
    switch (id) {
        case ChromeId::ToStart:      return to_start_btn_.get();
        case ChromeId::PrevFrame:    return prev_btn_.get();
        case ChromeId::Play:         return play_btn_.get();
        case ChromeId::Stop:         return stop_btn_.get();
        case ChromeId::NextFrame:    return next_btn_.get();
        case ChromeId::ToEnd:        return to_end_btn_.get();
        case ChromeId::MarkIn:       return mark_in_btn_.get();
        case ChromeId::MarkOut:      return mark_out_btn_.get();
        case ChromeId::Insert:       return insert_btn_.get();
        case ChromeId::Overwrite:    return overwrite_btn_.get();
        case ChromeId::Lift:         return lift_btn_.get();
        case ChromeId::Extract:      return extract_btn_.get();
        case ChromeId::SafeMargins:  return safe_btn_.get();
        case ChromeId::Grid:         return grid_btn_.get();
        case ChromeId::Loop:         return loop_btn_.get();
        case ChromeId::QualitySelect: return quality_select_.get();
        case ChromeId::ZoomSlider:   return zoom_slider_.get();
        case ChromeId::Timecode:     return timecode_.get();
        case ChromeId::Title:        return title_label_.get();
        case ChromeId::Meters:       return meter_.get();
    }
    return nullptr;
}

// ---- pushed state -------------------------------------------------------------

void MonitorPanel::SetTitle(std::string title) {
    title_label_->SetText(std::move(title));
}
void MonitorPanel::SetFps(int num, int den) {
    fps_num_ = num > 0 ? num : 30;
    fps_den_ = den > 0 ? den : 1;
}
void MonitorPanel::SetTimecode(double seconds) {
    timecode_seconds_ = seconds;
    static_cast<TimecodeLabel*>(timecode_.get())
        ->SetText(FormatTimecode(seconds, fps_num_, fps_den_));
}
void MonitorPanel::SetDuration(double seconds) {
    surface_->SetDuration(seconds);
}
void MonitorPanel::SetPlaying(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    SyncPlayButton();
}
void MonitorPanel::SyncPlayButton() {
    play_btn_->SetIcon(playing_ ? "transport.pause"
                                : "transport.play");
}
void MonitorPanel::SyncToggleButtons() {
    safe_btn_->SetChecked(surface_->SafeMargins());
    grid_btn_->SetChecked(surface_->Grid());
    loop_btn_->SetChecked(loop_enabled_);
}
void MonitorPanel::SetPeakLevels(float left, float right) {
    meter_->SetLevels(std::vector<W::LevelMeter::ChannelLevels>{
        {ToDb(left), ToDb(left)}, {ToDb(right), ToDb(right)}});
}
void MonitorPanel::SetQualityScale(float scale) {
    quality_scale_ = scale;
    syncing_quality_ = true;
    if (scale >= 0.99f) {
        quality_select_->SetSelectedIndex(0);
        quality_badge_->SetText("Full");
    } else if (scale >= 0.49f) {
        quality_select_->SetSelectedIndex(1);
        quality_badge_->SetText("1/2");
    } else {
        quality_select_->SetSelectedIndex(2);
        quality_badge_->SetText("1/4");
    }
    syncing_quality_ = false;
}
void MonitorPanel::SetProxyBadge(std::string text) {
    proxy_badge_->SetText(std::move(text));
    proxy_badge_->SetVisible(
        !proxy_badge_->GetText().empty());
    RequestLayout();
}
void MonitorPanel::SetStatusText(std::string text, bool is_error) {
    surface_->SetStatusText(std::move(text), is_error);
}
void MonitorPanel::SetSafeMargins(bool on) {
    surface_->SetSafeMargins(on);
    safe_btn_->SetChecked(on);
}
void MonitorPanel::SetGrid(bool on) {
    surface_->SetGrid(on);
    grid_btn_->SetChecked(on);
}
void MonitorPanel::SetLoopEnabled(bool on) {
    loop_enabled_ = on;
    surface_->SetLoop(on);
    loop_btn_->SetChecked(on);
}
void MonitorPanel::SetLoopRange(double in_s, double out_s) {
    surface_->SetLoopRange(in_s, out_s);
}
void MonitorPanel::SetWorkArea(double in_s, double out_s) {
    surface_->SetWorkArea(in_s, out_s);
}
void MonitorPanel::SetMarkers(const std::vector<Marker>& markers) {
    surface_->SetMarkers(markers);
}
void MonitorPanel::SetMarks(double in_s, double out_s) {
    surface_->SetMarks(in_s, out_s);
}
std::pair<double, double> MonitorPanel::Marks() const {
    return surface_->Marks();
}
void MonitorPanel::SetFrame(const std::vector<std::uint8_t>& bgra,
                            int width, int height) {
    surface_->SetFrame(bgra, width, height);
}
void MonitorPanel::SetClickPointsArmed(bool armed) {
    surface_->SetClickPointsArmed(armed);
}
void MonitorPanel::SetMarkPoints(
    std::vector<std::pair<int, int>> frame_points) {
    surface_->SetMarkPoints(std::move(frame_points));
}
void MonitorPanel::SetMaskContour(
    std::vector<Lab::IntPoint> frame_points) {
    surface_->SetMaskContour(std::move(frame_points));
}
void MonitorPanel::SetMaskOverlay(
    std::shared_ptr<const std::vector<std::uint8_t>> mask, int width,
    int height) {
    surface_->SetMaskOverlay(std::move(mask), width, height);
}

// ---- geometry -----------------------------------------------------------------

XER::UI::Layout::Rect MonitorPanel::HeaderRect() const {
    const auto b = GetBounds();
    return {b.x, b.y, b.width, kHeaderHeight};
}
XER::UI::Layout::Rect MonitorPanel::TransportRect() const {
    const auto b = GetBounds();
    return {b.x, b.y + b.height - kTransportHeight, b.width,
            kTransportHeight};
}
XER::UI::Layout::Rect MonitorPanel::MetersRect() const {
    const auto b = GetBounds();
    return {b.x + b.width - kMetersWidth,
            b.y + kHeaderHeight, kMetersWidth,
            std::max(0.0f, b.height - kHeaderHeight -
                                   kTransportHeight)};
}
XER::UI::Layout::Rect MonitorPanel::BodyRect() const {
    const auto b = GetBounds();
    return {b.x, b.y + kHeaderHeight,
            std::max(0.0f, b.width - kMetersWidth),
            std::max(0.0f, b.height - kHeaderHeight -
                                   kTransportHeight)};
}

float MonitorPanel::CenterStripLeft(float row_width) const {
    float strip = 0.0f;
    for (float w : kCenterW) strip += w;
    strip += kGap * (kCenterW.size() - 1);
    return (row_width - strip) * 0.5f;
}

XER::UI::Layout::Rect MonitorPanel::ChromeRect(ChromeId id) const {
    const auto row = TransportRect();
    const float y = row.y + (row.height - 28.0f) * 0.5f;
    // Center strip — index lookup.
    for (std::size_t i = 0; i < kCenterStrip.size(); ++i) {
        if (kCenterStrip[i] != id) continue;
        float x = row.x + CenterStripLeft(row.width);
        for (std::size_t j = 0; j < i; ++j) x += kCenterW[j] + kGap;
        return {x, y, kCenterW[i], 28.0f};
    }
    if (id == ChromeId::Timecode) {
        return {row.x + 4.0f, y, kTimecodeWidth, 28.0f};
    }
    // Right cluster — the prefix that survives the narrow-row drop;
    // ChromeRect reports the would-be slot regardless of visibility
    // (Layout hides dropped entries).
    const auto& order = kind_ == Kind::Program ? kRightClusterProgram
                                               : kRightCluster;
    float right_edge = row.x + row.width - 4.0f;
    // Walk right-to-left accumulating widths.
    float right_of_id = 0.0f;
    bool found = false;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (*it == ChromeId::Timecode) continue;
        const float w = ChromeWidth(*it);
        if (*it == id) {
            found = true;
            break;
        }
        right_of_id += w + kGap;
    }
    if (!found) return {};
    return {right_edge - right_of_id - ChromeWidth(id), y,
            ChromeWidth(id), 28.0f};
}

// ---- layout -----------------------------------------------------------------

void MonitorPanel::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();

    // Header: title left, badges right-aligned.
    const auto header = HeaderRect();
    float rx = header.x + header.width - 6.0f;
    const float badge_y =
        header.y + (header.height - kBadgeH) * 0.5f;
    if (quality_badge_->IsVisible()) {
        rx -= 44.0f;
        quality_badge_->Layout(
            {rx, badge_y, 44.0f, kBadgeH}, tokens);
    }
    if (proxy_badge_->IsVisible()) {
        rx -= 56.0f + kGap;
        proxy_badge_->Layout(
            {rx, badge_y, 56.0f, kBadgeH}, tokens);
    }
    title_label_->Layout(
        {header.x + 8.0f, header.y,
         std::max(0.0f, rx - header.x - 12.0f), header.height},
        tokens);

    // Body: surface flex + meters strip.
    const auto body = BodyRect();
    surface_->Layout(body, tokens);
    meter_->Layout(MetersRect(), tokens);

    // Transport row.
    const auto row = TransportRect();
    timecode_->Layout(ChromeRect(ChromeId::Timecode), tokens);
    for (std::size_t i = 0; i < kCenterStrip.size(); ++i) {
        auto* w = Chrome(kCenterStrip[i]);
        if (w != nullptr) {
            w->Layout(ChromeRect(kCenterStrip[i]), tokens);
        }
    }
    // Right cluster — prefix drop when the row narrows: measure the
    // space between the centered strip's right edge and the row edge.
    const auto& order = kind_ == Kind::Program ? kRightClusterProgram
                                               : kRightCluster;
    const float strip_right =
        row.x + CenterStripLeft(row.width) +
        (kCenterW[0] + kCenterW[1] + kCenterW[2] + kCenterW[3] +
         kCenterW[4] + kCenterW[5] + 5 * kGap);
    const float avail = row.x + row.width - 4.0f - strip_right - kGap;
    // Keep items while they fit walking right-to-left priority:
    // quality + toggles first, then edits — tail drop order is
    // simply the compiled order's tail.
    float needed = 0.0f;
    int kept = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == ChromeId::Timecode) continue;
        auto* w = Chrome(order[i]);
        if (w == nullptr) continue;   // scope-hidden slot
        needed += ChromeWidth(order[i]) + (needed > 0 ? kGap : 0.0f);
        if (needed <= avail || i < 2u) {
            // The mark buttons never drop — an empty cluster still
            // keeps the edit identity visible.
            ++kept;
        } else {
            break;
        }
    }
    int placed = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == ChromeId::Timecode) continue;
        auto* w = Chrome(order[i]);
        if (w == nullptr) continue;
        const bool show = placed < kept;
        w->SetVisible(show);
        if (show) w->Layout(ChromeRect(order[i]), tokens);
        ++placed;
    }
}

// ---- paint / events ------------------------------------------------------------

void MonitorPanel::Render(Rendering::Renderer& renderer,
                          const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    renderer.FillRect(b, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(b, tokens.border, 1.0f, tokens.radius_large);
    // Separators under the header and above the transport row.
    const auto header = HeaderRect();
    const auto row = TransportRect();
    renderer.DrawLine({b.x, header.y + header.height - 0.5f},
                      {b.x + b.width, header.y + header.height - 0.5f},
                      tokens.border, 1.0f);
    renderer.DrawLine({b.x, row.y + 0.5f}, {b.x + b.width, row.y + 0.5f},
                      tokens.border, 1.0f);
}

bool MonitorPanel::HandleEvent(const W::Event& event) {
    // Presses that land between children (header, meters, gaps) still
    // claim the transport scope — the buttons fire Activate() on
    // their own before the panel sees anything.
    if (event.type == W::Event::Type::MouseDown &&
        event.mouse_button == W::MouseButton::Left) {
        Activate();
        return true;   // inside our chrome — swallow stray presses
    }
    return false;   // right-clicks bubble to the tree's menu handler
}

void MonitorPanel::Tick(double dt_seconds) {
    (void)dt_seconds;   // LevelMeter runs its own ballistics in Tick
                        // via the tree's TickTree walk.
}

}  // namespace XER::VideoEditor::Ui2
