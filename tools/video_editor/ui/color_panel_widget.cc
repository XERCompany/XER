#include "color_panel_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "app_runtime/app_runtime.h"
#include "helper/path_utils.h"
#include "icon_helpers.h"
#include "lut_manager.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"
#include "widgets/slider.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Ocio = XER::VideoEditor::Color;
namespace Path = XER::Helper::Path;
namespace Rendering = XER::UI::Rendering;

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

// Two-line catalog row: body-size name over a caption-size subtitle.
constexpr float kRowHeight = VeTheme::kFontBody * 1.4f +
                             VeTheme::kFontCaption * 1.4f +
                             VeTheme::kSpaceXs;
constexpr float kHeaderHeight = VeTheme::kRowHeight;
constexpr float kRowIcon = VeTheme::kSpaceLg;
constexpr float kActionWidth = 100.0f;   // "Add to Clip" button

// ---- correction section geometry ----
constexpr float kWheelSize = 128.0f;      // square the disc fills
constexpr int kWheelBmpPx = 132;          // rasterized disc size
constexpr float kSliderLabelW = 80.0f;    // "Temperature" caption col
constexpr float kSliderValueW = 48.0f;    // mono readout col
constexpr float kSliderRowH = 26.0f;
// Correction ClipEffect types — kept as constants so a host
// replace-by-type pass stays coherent with what the panel emits.
constexpr const char* kWheelEffectType = "color_balance";
constexpr const char* kTempEffectType = "plugin:xer.fx.temperature";
constexpr const char* kTintEffectType = "plugin:xer.fx.tint";

// Last path segment of `path` — color.yaml LUT paths may use either
// separator style regardless of platform.
std::string Basename(const std::string& path) {
    const std::string::size_type pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Row subtitle: the LUT file name for looks, or the effective
// colorspace chain — empty profile fields inherit the config's
// source/display/view defaults.
std::string TagFor(const Ocio::OcioProfile& profile,
                   const Ocio::OcioConfig& cfg) {
    if (!profile.lut_path.empty()) {
        std::string tag = Basename(profile.lut_path);
        // LUT-manager entries carry their parsed size — "file · 33³"
        // for a 3D LUT, "file · 1D" for a one-dimensional table.
        if (const Lut::Entry* entry =
                Lut::Manager::Shared().FindByPath(profile.lut_path)) {
            if (entry->size > 0) {
                tag += entry->is_3d
                           ? " \xc2\xb7 " + std::to_string(entry->size) +
                                 "\xc2\xb3"
                           : " \xc2\xb7 1D";
            }
        }
        return tag;
    }
    const std::string& src =
        profile.source.empty() ? cfg.source : profile.source;
    const std::string& dst =
        profile.display.empty() ? cfg.display : profile.display;
    const std::string& view =
        profile.view.empty() ? cfg.view : profile.view;
    return src + " → " + dst + "/" + view;
}

// HSV -> RGB, all components in [0,1]; h wraps at 360.
void HsvToRgb(float h, float s, float v, float* r, float* g, float* b) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float rr = 0.0f, gg = 0.0f, bb = 0.0f;
    if (h < 60.0f)       { rr = c; gg = x; }
    else if (h < 120.0f) { rr = x; gg = c; }
    else if (h < 180.0f) { gg = c; bb = x; }
    else if (h < 240.0f) { gg = x; bb = c; }
    else if (h < 300.0f) { rr = x; bb = c; }
    else                 { rr = c; bb = x; }
    *r = rr + m;
    *g = gg + m;
    *b = bb + m;
}

// Leading-slot glyph for a row Item: draws its section's pack icon
// centered in the slot. Disabled so hit-testing skips it (the same
// convention as MarkersPanelWidget::MarkerSwatch) — row clicks and
// hover land on the Item itself.
class RowIcon final : public XER::UI::Widgets::Widget {
public:
    explicit RowIcon(std::string name) : name_(std::move(name)) {
        SetFocusable(false);
        SetEnabled(false);
    }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        VeIcons::DrawIcon(renderer, name_.c_str(),
                          static_cast<int>(kRowIcon), tokens.foreground,
                          GetBounds());
    }
    void Layout(const Rect& bounds) override {
        SetBounds(bounds);
    }
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    XER::UI::Layout::Size GetMinSize() const override {
        return {kRowIcon, kRowIcon};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {kRowIcon, kRowIcon};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {kRowIcon, kRowIcon};
    }

private:
    std::string name_;
};

}  // namespace

// The ScrollArea's content: drawn collapsible section headers
// interleaved with Item rows, plus the selection/hover chrome painted
// behind rows — children render after this widget's own pass, so the
// chrome reads as the rows' background exactly like the old drawn
// list. Deliberately not focusable: click focus must climb to the
// panel so arrow-key navigation keeps working after a row click.
class ColorPanelWidget::RowList final : public XER::UI::Widgets::Widget {
public:
    RowList() {
        SetFocusable(false);
    }

    // One display line: either a drawn collapsible header or a row
    // Item. `entry_index` points back into the panel's entries_ (row
    // indices differ once a group collapses).
    struct Line {
        bool header = false;
        bool collapsed = false;
        int entry_index = -1;
        std::string header_text;
        std::shared_ptr<XER::UI::Widgets::Item> item;   // rows only
    };

    // Adopts the row Items as children and swaps the line model.
    void SetLines(std::vector<Line> lines) {
        // Retire the previous generation — disable first so a stale
        // mouse capture can't fire a detached row, then keep them
        // alive one rebuild (the MarkersPanelWidget pattern).
        retired_.clear();
        for (Line& line : lines_) {
            if (line.item == nullptr) continue;
            line.item->SetEnabled(false);
            RemoveChild(line.item);
            retired_.push_back(line.item);
        }
        lines_ = std::move(lines);
        for (const Line& line : lines_) {
            if (line.item != nullptr) AddChild(line.item);
        }
        RequestLayout();
        Invalidate();
    }

    // Selected ENTRY index (-1 = none); header entries are simply
    // never passed.
    void SetSelected(int entry_index) {
        if (selected_ == entry_index) return;
        selected_ = entry_index;
        Invalidate();
    }

    // Header clicks toggle a group — the panel flips its collapsed
    // set and rebuilds the lines.
    void SetOnHeaderToggle(
        std::function<void(const std::string& header_text)> cb) {
        on_header_toggle_ = std::move(cb);
    }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const Line& line = lines_[i];
            const auto& r = line_rects_[i];
            if (line.header) {
                VeIcons::DrawSectionHeader(renderer, tokens, r,
                                           line.header_text,
                                           line.collapsed);
                continue;
            }
            const auto row = line.item->GetBounds();
            if (line.entry_index == selected_) {
                renderer.FillRect(row, tokens.selection,
                                  tokens.radius_small);
                renderer.StrokeRect(
                    row, VeTheme::WithAlpha(tokens.accent, 0.5f), 1.0f,
                    tokens.radius_small);
            } else if (line.item->IsHovered()) {
                renderer.FillRect(row, tokens.hover, tokens.radius_small);
            }
        }
    }

    void Layout(const Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        line_rects_.clear();
        line_rects_.reserve(lines_.size());
        float y = bounds.y;
        for (Line& line : lines_) {
            const float h = line.header ? kHeaderHeight : kRowHeight;
            line_rects_.push_back({bounds.x, y, bounds.width, h});
            if (line.item != nullptr) {
                line.item->Layout(
                    {bounds.x + VeTheme::kSpaceSm,
                     y + VeTheme::kSpaceXxs,
                     std::max(0.0f,
                              bounds.width - VeTheme::kSpaceSm * 2.0f),
                     kRowHeight - VeTheme::kSpaceXs},
                    tokens);
            }
            y += h;
        }
    }
    using Widget::Layout;

    bool HandleEvent(const XER::UI::Widgets::Event& event) override {
        // Rows consume their own clicks (Item::SetOnClick). A click on
        // a drawn header line toggles its group; gaps stay
        // transparent so clicks bubble like the drawn list's.
        if (event.type == XER::UI::Widgets::Event::Type::Click &&
            event.mouse_button == XER::UI::Widgets::MouseButton::Left) {
            for (std::size_t i = 0; i < lines_.size(); ++i) {
                const Line& line = lines_[i];
                if (line.header && i < line_rects_.size() &&
                    line_rects_[i].Contains(event.position)) {
                    if (on_header_toggle_) {
                        on_header_toggle_(line.header_text);
                    }
                    return true;
                }
            }
        }
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {0.0f, 0.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {0.0f, GetContentHeight()};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override {
        float h = 0.0f;
        for (const Line& line : lines_) {
            h += line.header ? kHeaderHeight : kRowHeight;
        }
        return h;
    }

private:
    std::vector<Line> lines_;
    // Row Items removed by the last SetLines — kept alive one rebuild
    // in case an event was mid-dispatch through them.
    std::vector<std::shared_ptr<XER::UI::Widgets::Item>> retired_;
    std::vector<Rect> line_rects_;
    int selected_ = -1;
    std::function<void(const std::string&)> on_header_toggle_;
};

std::shared_ptr<ColorPanelWidget> ColorPanelWidget::Create() {
    return std::shared_ptr<ColorPanelWidget>(new ColorPanelWidget());
}

ColorPanelWidget::~ColorPanelWidget() = default;

ColorPanelWidget::ColorPanelWidget() {
    SetFocusable(true);
    SetClipsChildren(true);   // the list scrolls inside the panel
    Refresh();
}

void ColorPanelWidget::SetOnAddEffect(
    std::function<void(const std::string&)> cb) {
    on_add_effect_ = std::move(cb);
}

void ColorPanelWidget::SetOnCorrection(
    std::function<void(const ClipEffect&)> cb) {
    on_correction_ = std::move(cb);
}

void ColorPanelWidget::SetOnSelectionChanged(
    std::function<void(const std::string&)> cb) {
    on_selection_changed_ = std::move(cb);
}

void ColorPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    ocio_state_ = std::make_shared<Widgets::Label>(
        ocio_enabled_ ? "Display Transform: ON"
                      : "Display Transform: OFF",
        true);
    ocio_state_->SetEnabled(false);   // readout on the header row
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add to Clip", Widgets::ButtonStyle::Primary);
    add_btn_->SetTooltip("Add the selected color profile to the clip");
    add_btn_->SetOnClick([this] { AddSelected(); });
    status_ = std::make_shared<Widgets::Label>(
        entries_.empty() ? "No profiles found" : "Select a profile",
        true);

    // Correction sliders — -100..100 to match the plugin params. A
    // committed gesture emits one parameterized ClipEffect; drags
    // commit on release (Tick watches IsDragging) so undo gets a
    // single step per gesture, clicks/arrow keys commit immediately.
    const auto make_slider = [this](const char* type,
                                    bool* pending) {
        auto slider = std::make_shared<Widgets::Slider>(-100.0f, 100.0f,
                                                        0.0f);
        slider->SetTooltip(
            "Release applies the correction to the selected clip");
        // Capture the raw pointer — a shared_ptr here would cycle
        // slider -> callback -> slider.
        Widgets::Slider* raw = slider.get();
        slider->SetOnValueChanged([this, type, pending,
                                   raw](float value) {
            if (raw->IsDragging()) {
                *pending = true;
            } else {
                CommitSliderEffect(type, value);
            }
        });
        return slider;
    };
    temp_slider_ = make_slider(kTempEffectType, &temp_pending_commit_);
    tint_slider_ = make_slider(kTintEffectType, &tint_pending_commit_);

    list_ = std::make_shared<RowList>();
    list_->SetOnHeaderToggle([this](const std::string& header) {
        if (collapsed_groups_.count(header) != 0) {
            collapsed_groups_.erase(header);
        } else {
            collapsed_groups_.insert(header);
        }
        RebuildList();
    });
    scroll_area_ = std::make_shared<Widgets::ScrollArea>(list_);

    AddChild(ocio_state_);
    AddChild(temp_slider_);
    AddChild(tint_slider_);
    AddChild(scroll_area_);
    AddChild(add_btn_);
    AddChild(status_);
    RebuildList();
    UpdateAddEnabled();
}

void ColorPanelWidget::Refresh() {
    // Keep the selection across a reload when its name still exists.
    const std::string keep =
        selected_ >= 0 && selected_ < static_cast<int>(entries_.size())
            ? entries_[static_cast<std::size_t>(selected_)].name
            : std::string();

    // Display-transform state + colorspace defaults for subtitles —
    // compiled defaults stand in when color.yaml is absent/failing.
    Ocio::OcioConfig cfg;
    const std::string& yaml =
        AppRuntime::SharedAssetsConfig().color_yaml;
    std::string error;
    ocio_enabled_ = !yaml.empty() &&
                    Ocio::LoadOcioConfig(Path::FromUtf8(yaml), &cfg,
                                         &error) &&
                    cfg.enabled;

    entries_.clear();
    int profile_count = 0;
    const auto& profiles = Ocio::SharedOcioProfiles();
    // Header entries stash their group icon name (assets/icons.yaml)
    // in `tag` — headers never render a tag line, and the icon names
    // double as the section icon for the rows that follow (same
    // convention as the plugins panel).
    const auto append_group = [&](bool looks, const char* label,
                                  const char* icon) {
        bool headed = false;
        for (const Ocio::OcioProfile& profile : profiles) {
            if (looks != !profile.lut_path.empty()) continue;
            if (!headed) {
                entries_.push_back({true, label, icon, {}});
                headed = true;
            }
            entries_.push_back({false, profile.name,
                                TagFor(profile, cfg), profile.name});
            ++profile_count;
        }
    };
    append_group(true, "Looks", "shadow");
    append_group(false, "Transforms", "compare_arrows");

    selected_ = -1;
    if (!keep.empty()) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (!entries_[i].header && entries_[i].name == keep) {
                selected_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (children_attached_) {
        RebuildList();
        UpdateAddEnabled();
        ocio_state_->SetText(ocio_enabled_
                                 ? "Display Transform: ON"
                                 : "Display Transform: OFF");
        status_->SetText(profile_count == 0
                             ? "No profiles found"
                             : std::to_string(profile_count) +
                                   " profile(s)");
    }
    Invalidate();
}

bool ColorPanelWidget::EntryHidden(int index) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return true;
    }
    // Rows fold under the collapsed header that precedes them.
    bool hidden = false;
    for (int i = 0; i <= index; ++i) {
        if (entries_[static_cast<std::size_t>(i)].header) {
            hidden = collapsed_groups_.count(
                         entries_[static_cast<std::size_t>(i)].text) !=
                     0;
        }
    }
    return hidden && !entries_[static_cast<std::size_t>(index)].header;
}

void ColorPanelWidget::RebuildList() {
    std::vector<RowList::Line> lines;
    lines.reserve(entries_.size());
    // The section icon travels from the header into each row's leading
    // slot — same sharing the drawn list had via section_icon.
    std::string section_icon;
    bool collapsed = false;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        if (entry.header) {
            section_icon = entry.tag;
            collapsed =
                collapsed_groups_.count(entry.text) != 0;
            lines.push_back({true, collapsed,
                             static_cast<int>(i), entry.text, nullptr});
            continue;
        }
        if (collapsed) continue;   // folded group: skip the row
        auto item = std::make_shared<XER::UI::Widgets::Item>();
        item->SetTitle(entry.text);
        item->SetDescription(entry.tag);
        item->SetTooltip(entry.text);
        if (!section_icon.empty()) {
            item->AddChild(std::make_shared<RowIcon>(section_icon));
        }
        const int index = static_cast<int>(i);
        item->SetOnClick([this, index] { SelectRow(index); });
        item->SetOnDoubleClick([this] { AddSelected(); });
        lines.push_back({false, false, index, {}, std::move(item)});
    }
    list_->SetLines(std::move(lines));
    list_->SetSelected(selected_);
}

void ColorPanelWidget::UpdateAddEnabled() {
    add_btn_->SetEnabled(
        selected_ >= 0 &&
        selected_ < static_cast<int>(entries_.size()) &&
        !entries_[static_cast<std::size_t>(selected_)].header);
}

void ColorPanelWidget::EmitSelection() {
    if (!on_selection_changed_) return;
    on_selection_changed_(
        selected_ >= 0 &&
                selected_ < static_cast<int>(entries_.size()) &&
                !entries_[static_cast<std::size_t>(selected_)].header
            ? entries_[static_cast<std::size_t>(selected_)].name
            : std::string());
}

float ColorPanelWidget::EntryHeight(int index) const {
    return entries_[static_cast<std::size_t>(index)].header
               ? kHeaderHeight
               : kRowHeight;
}

float ColorPanelWidget::EntryTop(int index) const {
    float y = 0.0f;
    for (int i = 0; i < index; ++i) {
        // Collapsed rows are absent from the list — their share of the
        // content height doesn't exist.
        if (!entries_[static_cast<std::size_t>(i)].header &&
            EntryHidden(i)) {
            continue;
        }
        y += EntryHeight(i);
    }
    return y;
}

void ColorPanelWidget::SelectRow(int entry_index) {
    if (selected_ == entry_index) return;
    selected_ = entry_index;
    UpdateAddEnabled();
    EmitSelection();
    if (children_attached_) list_->SetSelected(selected_);
    Invalidate();
}

void ColorPanelWidget::EnsureSelectionVisible() {
    if (scroll_area_ == nullptr || selected_ < 0) return;
    const float view_h = scroll_area_->GetBounds().height;
    const float top = EntryTop(selected_);
    const float bottom = top + EntryHeight(selected_);
    const float scroll = scroll_area_->GetScrollY();
    if (top < scroll) {
        scroll_area_->ScrollTo(top);
    } else if (bottom > scroll + view_h) {
        scroll_area_->ScrollTo(bottom - view_h);
    }
}

void ColorPanelWidget::AddSelected() {
    if (selected_ < 0 ||
        selected_ >= static_cast<int>(entries_.size()) ||
        entries_[static_cast<std::size_t>(selected_)].header) {
        SetStatus("Select a profile");
        return;
    }
    const Entry& entry = entries_[static_cast<std::size_t>(selected_)];
    if (on_add_effect_) {
        on_add_effect_("ocio:" + entry.name);
    }
    SetStatus("added " + entry.text);
}

void ColorPanelWidget::SetStatus(const std::string& text) {
    if (children_attached_) status_->SetText(text);
}

// ---- correction section ----

Rect ColorPanelWidget::WheelRect() const {
    return wheel_rect_;
}

void ColorPanelWidget::EnsureWheelBitmap() {
    if (!wheel_bmp_.empty()) return;
    // Hue/saturation disc: angle -> hue, radius -> saturation (value
    // pinned at 1). Premultiplied BGRA — DrawFrameBgra uploads to a
    // premultiplied D2D bitmap, so edge pixels fade out instead of
    // ringing.
    wheel_bmp_.assign(
        static_cast<std::size_t>(kWheelBmpPx) * kWheelBmpPx * 4, 0);
    const float half = kWheelBmpPx * 0.5f;
    const float max_r = half - 0.5f;
    for (int py = 0; py < kWheelBmpPx; ++py) {
        for (int px = 0; px < kWheelBmpPx; ++px) {
            const float dx = px + 0.5f - half;
            const float dy = py + 0.5f - half;
            const float r = std::sqrt(dx * dx + dy * dy) / max_r;
            if (r > 1.0f) continue;
            const float hue =
                std::atan2(dy, dx) * 180.0f / 3.14159265358979f;
            float rr, gg, bb;
            HsvToRgb(hue, r, 1.0f, &rr, &gg, &bb);
            // 1px antialiased rim.
            const float a =
                std::clamp((1.0f - r) * max_r, 0.0f, 1.0f);
            auto* out =
                wheel_bmp_.data() +
                (static_cast<std::size_t>(py) * kWheelBmpPx + px) * 4;
            out[0] = static_cast<std::uint8_t>(bb * a * 255.0f);
            out[1] = static_cast<std::uint8_t>(gg * a * 255.0f);
            out[2] = static_cast<std::uint8_t>(rr * a * 255.0f);
            out[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    }
}

void ColorPanelWidget::UpdateWheelFromPoint(const Point& position) {
    const Rect wheel = wheel_rect_;
    const float cx = wheel.x + wheel.width * 0.5f;
    const float cy = wheel.y + wheel.height * 0.5f;
    const float max_r = wheel.width * 0.5f;
    const float dx = position.x - cx;
    const float dy = position.y - cy;
    const float r = std::sqrt(dx * dx + dy * dy);
    wheel_amount_ = max_r > 0.0f ? std::clamp(r / max_r, 0.0f, 1.0f)
                                 : 0.0f;
    // Hue is undefined at the exact center — keep the last angle.
    if (r > 2.0f) {
        wheel_hue_deg_ =
            std::atan2(dy, dx) * 180.0f / 3.14159265358979f;
        if (wheel_hue_deg_ < 0.0f) wheel_hue_deg_ += 360.0f;
    }
    Invalidate();
}

ClipEffect ColorPanelWidget::WheelEffect() const {
    // Lift toward the picked hue: fully-saturated hue color, recentered
    // around neutral and scaled by the radius — a drag to the edge
    // pushes a ±0.5 channel offset.
    float rr, gg, bb;
    HsvToRgb(wheel_hue_deg_, 1.0f, 1.0f, &rr, &gg, &bb);
    ClipEffect effect;
    effect.type = kWheelEffectType;
    effect.params = {(rr - 0.5f) * wheel_amount_,
                     (gg - 0.5f) * wheel_amount_,
                     (bb - 0.5f) * wheel_amount_,
                     1.0f, 1.0f, 1.0f};
    return effect;
}

void ColorPanelWidget::CommitSliderEffect(const std::string& type,
                                          float value) {
    ClipEffect effect;
    effect.type = type;
    effect.params = {std::clamp(value, -100.0f, 100.0f)};
    EmitCorrection(effect);
}

void ColorPanelWidget::EmitCorrection(const ClipEffect& effect) {
    if (on_correction_) {
        on_correction_(effect);
        SetStatus("correction " + effect.type);
    } else {
        SetStatus("Select a clip to grade");
    }
}

void ColorPanelWidget::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    EnsureWheelBitmap();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);

    // "Correction" section header — chevron + bold caption (the shared
    // VeIcons header the other right-dock panels use).
    if (header_rect_.width > 0.0f) {
        VeIcons::DrawSectionHeader(renderer, tokens, header_rect_,
                                   "Correction", correction_collapsed_);
    }

    if (!correction_collapsed_) {
        // Color wheel: premultiplied hue/saturation disc + border ring
        // + draggable indicator (accent ring over the picked color).
        const Rect wheel = wheel_rect_;
        if (wheel.width > 4.0f) {
            renderer.DrawFrameBgra(wheel_bmp_.data(), kWheelBmpPx,
                                   kWheelBmpPx, wheel, 1.0f);
            const float cx = wheel.x + wheel.width * 0.5f;
            const float cy = wheel.y + wheel.height * 0.5f;
            const float radius = wheel.width * 0.5f;
            renderer.StrokeRect({cx - radius, cy - radius,
                                 radius * 2.0f, radius * 2.0f},
                                VeTheme::WithAlpha(tokens.border, 0.9f),
                                1.0f, radius);
            const float angle =
                wheel_hue_deg_ * 3.14159265358979f / 180.0f;
            const float ix =
                cx + std::cos(angle) * wheel_amount_ * radius;
            const float iy =
                cy + std::sin(angle) * wheel_amount_ * radius;
            float pr, pg, pb;
            HsvToRgb(wheel_hue_deg_, wheel_amount_, 1.0f, &pr, &pg,
                     &pb);
            constexpr float kDot = 11.0f;
            const Rect dot{ix - kDot * 0.5f, iy - kDot * 0.5f, kDot,
                           kDot};
            renderer.FillRect(dot, {pr, pg, pb, 1.0f}, kDot * 0.5f);
            renderer.StrokeRect(
                dot,
                wheel_drag_ ? tokens.accent
                            : VeTheme::WithAlpha(tokens.foreground, 0.9f),
                wheel_drag_ ? 2.0f : 1.5f, kDot * 0.5f);
        }

        // Slider rows: caption left, Slider child mid, monospace value
        // right — the labeled readout style from the reference.
        const Rendering::TextStyle label_style{
            tokens.foreground, tokens.font_small, 500};
        const Rendering::TextStyle value_style{
            tokens.foreground, tokens.font_small, 400,
            /*monospace=*/true};
        const auto draw_row = [&](const char* label,
                                  const std::shared_ptr<
                                      XER::UI::Widgets::Slider>& slider,
                                  const char* fmt) {
            const Rect sb = slider->GetBounds();
            if (sb.width <= 0.0f) return;
            renderer.DrawText(
                label,
                {sb.x - kSliderLabelW, sb.y, kSliderLabelW - 4.0f,
                 sb.height},
                label_style);
            char value[24];
            std::snprintf(value, sizeof(value), fmt,
                          static_cast<double>(slider->GetValue()));
            const Rect vr{sb.Right() + VeTheme::kSpaceSm, sb.y,
                          kSliderValueW, sb.height};
            // Measure to right-align the mono readout in its column.
            const auto size = renderer.MeasureText(value, value_style);
            renderer.DrawText(
                value,
                {vr.Right() - std::min(size.width, vr.width), vr.y,
                 std::min(size.width, vr.width), vr.height},
                value_style);
        };
        draw_row("Temperature", temp_slider_, "%+.1f");
        draw_row("Tint", tint_slider_, "%+.1f");
    }

    if (entries_.empty()) {
        const Rendering::TextStyle style{
            tokens.muted, tokens.font_small, 400};
        const char* hint = "No profiles — check assets/color.yaml";
        const auto size = renderer.MeasureText(hint, style);
        const Rect area =
            scroll_area_ != nullptr ? scroll_area_->GetBounds()
                                    : bounds;
        renderer.DrawText(
            hint,
            {area.x +
                 std::max(0.0f, (area.width - size.width) * 0.5f),
             area.y +
                 std::max(0.0f, (area.height - size.height) * 0.5f),
             size.width, size.height},
            style);
    }
    // Rows paint themselves — Item children inside the ScrollArea;
    // the RowList draws collapsible headers + selection/hover chrome.
}

void ColorPanelWidget::Layout(const Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;
    float y = bounds.y + pad * 0.6f;

    // "Correction" header row — the chevron/title is drawn, the OCIO
    // state readout sits right-aligned on the same band.
    header_rect_ = {inner_x, y, inner_w, kHeaderHeight};
    ocio_state_->Layout({inner_x + inner_w * 0.5f, y, inner_w * 0.5f,
                         kHeaderHeight},
                        tokens);
    y += kHeaderHeight + VeTheme::kSpaceXs;

    const float footer_h = VeTheme::kControlHeight;
    const float footer_y =
        bounds.y + bounds.height - footer_h - VeTheme::kSpaceXs;

    if (!correction_collapsed_) {
        // Wheel: centered square capped at kWheelSize so the disc
        // doesn't blow up on wide docks.
        const float wheel_side = std::min(inner_w, kWheelSize);
        wheel_rect_ = {inner_x + (inner_w - wheel_side) * 0.5f, y,
                       wheel_side, wheel_side};
        y += wheel_side + gap;

        // Slider rows: [label 80][slider flex][value 48].
        const float slider_w = std::max(
            40.0f, inner_w - kSliderLabelW - kSliderValueW - gap);
        const float slider_x = inner_x + kSliderLabelW;
        temp_slider_->Layout(
            {slider_x, y + (kSliderRowH - 20.0f) * 0.5f, slider_w,
             20.0f},
            tokens);
        y += kSliderRowH;
        tint_slider_->Layout(
            {slider_x, y + (kSliderRowH - 20.0f) * 0.5f, slider_w,
             20.0f},
            tokens);
        y += kSliderRowH + gap;
    } else {
        wheel_rect_ = {};
        temp_slider_->Layout({0.0f, 0.0f, 0.0f, 0.0f}, tokens);
        tint_slider_->Layout({0.0f, 0.0f, 0.0f, 0.0f}, tokens);
    }
    temp_slider_->SetVisible(!correction_collapsed_);
    tint_slider_->SetVisible(!correction_collapsed_);

    scroll_area_->Layout(
        {inner_x, y, inner_w, std::max(0.0f, footer_y - gap - y)},
        tokens);
    add_btn_->Layout({inner_x, footer_y, kActionWidth, footer_h}, tokens);
    status_->Layout(
        {inner_x + kActionWidth + gap, footer_y + VeTheme::kSpaceXxs,
         std::max(40.0f, inner_w - kActionWidth - gap),
         footer_h - VeTheme::kSpaceXs},
        tokens);
}

bool ColorPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    namespace Widgets = XER::UI::Widgets;
    switch (event.type) {
        case Widgets::Event::Type::MouseDown: {
            if (event.mouse_button == Widgets::MouseButton::Left &&
                !correction_collapsed_ &&
                wheel_rect_.Contains(event.position)) {
                wheel_drag_ = true;
                UpdateWheelFromPoint(event.position);
                return true;
            }
            return false;
        }
        case Widgets::Event::Type::MouseMove: {
            if (wheel_drag_) {
                UpdateWheelFromPoint(event.position);
                return true;
            }
            return false;
        }
        case Widgets::Event::Type::MouseUp: {
            if (wheel_drag_ &&
                event.mouse_button == Widgets::MouseButton::Left) {
                wheel_drag_ = false;
                Invalidate();
                EmitCorrection(WheelEffect());
                return true;
            }
            return false;
        }
        case Widgets::Event::Type::Click: {
            // The drawn "Correction" header toggles its section.
            if (event.mouse_button == Widgets::MouseButton::Left &&
                header_rect_.Contains(event.position)) {
                correction_collapsed_ = !correction_collapsed_;
                RequestLayout();
                Invalidate();
                return true;
            }
            return false;
        }
        default:
            break;
    }
    // Row clicks are consumed by the Items themselves; this panel only
    // still owns keyboard navigation. Clicks bubbling up from dead
    // children land in `default` and stay unhandled — the same dead
    // zone the drawn list's gaps were.
    if (event.type == Widgets::Event::Type::KeyDown) {
        if (event.key == Widgets::KeyCode::Enter) {
            AddSelected();
            return true;
        }
        if (event.key == Widgets::KeyCode::ArrowDown ||
            event.key == Widgets::KeyCode::ArrowUp) {
            if (!entries_.empty()) {
                const int count = static_cast<int>(entries_.size());
                const int dir =
                    event.key == Widgets::KeyCode::ArrowDown
                        ? 1
                        : -1;
                // Step over section headers and collapsed rows until a
                // visible row is found.
                int i = selected_;
                for (int step = 0; step < count; ++step) {
                    i = i < 0 ? (dir > 0 ? 0 : count - 1)
                              : (i + dir + count) % count;
                    if (!entries_[static_cast<std::size_t>(i)].header &&
                        !EntryHidden(i)) {
                        SelectRow(i);
                        break;
                    }
                }
                EnsureSelectionVisible();
            }
            return true;
        }
    }
    return false;
}

void ColorPanelWidget::Tick(double dt_seconds) {
    (void)dt_seconds;
    // Drag-committed sliders fire once on release — polling IsDragging
    // here catches the MouseUp without each move being an undo step.
    if (children_attached_) {
        if (temp_pending_commit_ && !temp_slider_->IsDragging()) {
            temp_pending_commit_ = false;
            CommitSliderEffect(kTempEffectType,
                               temp_slider_->GetValue());
        }
        if (tint_pending_commit_ && !tint_slider_->IsDragging()) {
            tint_pending_commit_ = false;
            CommitSliderEffect(kTintEffectType,
                               tint_slider_->GetValue());
        }
    }
}

}  // namespace XER::VideoEditor::Ui
