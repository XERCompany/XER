#include "color_panel_widget.h"

#include <algorithm>
#include <utility>

#include "app_runtime/app_runtime.h"
#include "helper/path_utils.h"
#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Ocio = XER::VideoEditor::Color;
namespace Path = XER::Helper::Path;

// Two-line catalog row: body-size name over a caption-size subtitle.
constexpr float kRowHeight = VeTheme::kFontBody * 1.4f +
                             VeTheme::kFontCaption * 1.4f +
                             VeTheme::kSpaceXs;
constexpr float kHeaderHeight = VeTheme::kRowHeight;
constexpr float kHeaderIcon = VeTheme::kFontBody;
constexpr float kRowIcon = VeTheme::kSpaceLg;
constexpr float kActionWidth = 100.0f;   // "Add to Clip" button
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

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
    if (!profile.lut_path.empty()) return Basename(profile.lut_path);
    const std::string& src =
        profile.source.empty() ? cfg.source : profile.source;
    const std::string& dst =
        profile.display.empty() ? cfg.display : profile.display;
    const std::string& view =
        profile.view.empty() ? cfg.view : profile.view;
    return src + " → " + dst + "/" + view;
}

}  // namespace

std::shared_ptr<ColorPanelWidget> ColorPanelWidget::Create() {
    return std::shared_ptr<ColorPanelWidget>(new ColorPanelWidget());
}

ColorPanelWidget::ColorPanelWidget() {
    SetFocusable(true);
    SetClipsChildren(true);   // list scrolls inside the panel
    Refresh();
}

void ColorPanelWidget::SetOnAddEffect(
    std::function<void(const std::string&)> cb) {
    on_add_effect_ = std::move(cb);
}

void ColorPanelWidget::SetOnSelectionChanged(
    std::function<void(const std::string&)> cb) {
    on_selection_changed_ = std::move(cb);
}

void ColorPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("COLOR");
    ocio_state_ = std::make_shared<Widgets::Label>(
        ocio_enabled_ ? "Display Transform: ON"
                      : "Display Transform: OFF",
        true);
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add to Clip", Widgets::ButtonStyle::Primary);
    add_btn_->SetTooltip("Add the selected color profile to the clip");
    add_btn_->SetOnClick([this] { AddSelected(); });
    status_ = std::make_shared<Widgets::Label>(
        entries_.empty() ? "No profiles found" : "Select a profile",
        true);

    AddChild(title_);
    AddChild(ocio_state_);
    AddChild(add_btn_);
    AddChild(status_);
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
    append_group(true, "LOOKS", "shadow");
    append_group(false, "TRANSFORMS", "compare_arrows");

    content_height_ = 0.0f;
    for (const Entry& entry : entries_) {
        content_height_ += entry.header ? kHeaderHeight : kRowHeight;
    }
    scroll_y_ = std::clamp(scroll_y_, 0.0f,
                           std::max(0.0f, content_height_));
    hovered_ = -1;
    last_click_row_ = -1;

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
    for (int i = 0; i < index; ++i) y += EntryHeight(i);
    return y;
}

int ColorPanelWidget::RowAt(
    const XER::UI::Layout::Point& position) const {
    if (position.y < rows_top_ || position.y >= rows_bottom_) {
        return -1;
    }
    float y = rows_top_ - scroll_y_;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const float h = entries_[i].header ? kHeaderHeight : kRowHeight;
        if (position.y >= y && position.y < y + h) {
            return entries_[i].header ? -1 : static_cast<int>(i);
        }
        y += h;
    }
    return -1;
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

void ColorPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (entries_.empty()) {
        const XER::UI::Rendering::TextStyle style{
            tokens.muted, tokens.font_small, 400};
        const char* hint = "No profiles — check assets/color.yaml";
        const auto size = renderer.MeasureText(hint, style);
        renderer.DrawText(
            hint,
            {bounds.x +
                 std::max(0.0f, (bounds.width - size.width) * 0.5f),
             bounds.y +
                 std::max(0.0f, (bounds.height - size.height) * 0.5f),
             size.width, size.height},
            style);
        return;
    }

    // Rows are drawn (not child widgets) so clicks land on the panel —
    // verified: WidgetTree dispatches Click to the deepest hit target
    // only (widget_tree.cc); child widgets do not bubble it up. The
    // clip keeps a half-scrolled row from bleeding into the header or
    // footer band (same PushClip convention the inspector uses).
    if (rows_top_ <= 0.0f) return;   // not yet laid out
    const float inset = VeTheme::kSpaceSm;
    const float width = bounds.width - inset * 2.0f;
    const float title_h = tokens.font_body * 1.4f;
    const float sub_h = tokens.font_small * 1.4f;
    float y = rows_top_ - scroll_y_;
    // Icon of the section currently being walked — headers carry it in
    // `tag`; rows reuse it so a group and its items share a glyph.
    const char* section_icon = nullptr;
    renderer.PushClip({bounds.x, rows_top_, bounds.width,
                       rows_bottom_ - rows_top_});
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        const float h = EntryHeight(static_cast<int>(i));
        if (y + h < rows_top_ || y > rows_bottom_) {
            // Keep tracking the section even for clipped headers.
            if (entry.header && !entry.tag.empty()) {
                section_icon = entry.tag.c_str();
            }
            y += h;
            continue;
        }
        if (entry.header) {
            // Section header — the inspector's drawn-label convention:
            // muted, small, weight 600, vertically centered.
            float label_x = bounds.x + VeTheme::kSpaceMd;
            if (!entry.tag.empty()) {
                section_icon = entry.tag.c_str();
                if (VeIcons::DrawIcon(renderer, section_icon,
                             static_cast<int>(kHeaderIcon), tokens.muted,
                             {label_x,
                              y + (kHeaderHeight - kHeaderIcon) * 0.5f,
                              kHeaderIcon, kHeaderIcon})) {
                    label_x += kHeaderIcon + VeTheme::kSpaceXs;
                }
            }
            renderer.DrawText(entry.text,
                              {label_x,
                               y + (kHeaderHeight - sub_h) * 0.5f,
                               bounds.x + bounds.width - label_x -
                                   VeTheme::kSpaceMd,
                               sub_h},
                              XER::UI::Rendering::TextStyle{
                                  tokens.muted, tokens.font_small, 600});
        } else {
            const int idx = static_cast<int>(i);
            const XER::UI::Layout::Rect row{
                bounds.x + inset, y + VeTheme::kSpaceXxs, width,
                kRowHeight - VeTheme::kSpaceXs};
            if (idx == selected_) {
                renderer.FillRect(row, tokens.selection,
                                  tokens.radius_small);
                renderer.StrokeRect(
                    row, VeTheme::WithAlpha(tokens.accent, 0.5f), 1.0f,
                    tokens.radius_small);
            } else if (idx == hovered_) {
                renderer.FillRect(row, tokens.hover, tokens.radius_small);
            }
            float text_x = row.x + VeTheme::kSpaceSm;
            if (section_icon != nullptr &&
                VeIcons::DrawIcon(renderer, section_icon,
                         static_cast<int>(kRowIcon), tokens.foreground,
                         {row.x + VeTheme::kSpaceXs,
                          row.y + (row.height - kRowIcon) * 0.5f,
                          kRowIcon, kRowIcon})) {
                text_x = row.x + VeTheme::kSpaceXs + kRowIcon +
                         VeTheme::kSpaceSm;
            }
            const float text_w =
                row.x + row.width - text_x - VeTheme::kSpaceSm;
            renderer.DrawText(entry.text,
                              {text_x, row.y + VeTheme::kSpaceXxs, text_w,
                               title_h},
                              XER::UI::Rendering::TextStyle{
                                  tokens.foreground, tokens.font_body,
                                  500});
            renderer.DrawText(entry.tag,
                              {text_x,
                               row.y + VeTheme::kSpaceXxs + title_h,
                               text_w, sub_h},
                              XER::UI::Rendering::TextStyle{
                                  tokens.muted, tokens.font_small, 400});
        }
        y += h;
    }
    renderer.PopClip();
}

void ColorPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;
    float y = bounds.y + pad * 0.6f;

    title_->Layout({inner_x, y, inner_w, VeTheme::kRowHeight}, tokens);
    y += VeTheme::kRowHeight + VeTheme::kSpaceXxs;
    ocio_state_->Layout({inner_x, y, inner_w, VeTheme::kSpaceLg}, tokens);
    y += VeTheme::kSpaceLg + gap;
    rows_top_ = y;

    const float footer_h = VeTheme::kControlHeight;
    const float footer_y =
        bounds.y + bounds.height - footer_h - VeTheme::kSpaceXs;
    rows_bottom_ = footer_y - gap;
    add_btn_->Layout({inner_x, footer_y, kActionWidth, footer_h}, tokens);
    status_->Layout(
        {inner_x + kActionWidth + gap, footer_y + VeTheme::kSpaceXxs,
         std::max(40.0f, inner_w - kActionWidth - gap),
         footer_h - VeTheme::kSpaceXs},
        tokens);
    // Keep the list viewport out of the footer.
    const float view_h = footer_y - rows_top_ - gap;
    scroll_y_ = std::clamp(
        scroll_y_, 0.0f,
        std::max(0.0f, content_height_ - std::max(view_h, 0.0f)));
}

bool ColorPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    switch (event.type) {
        case XER::UI::Widgets::Event::Type::MouseWheel: {
            const float view_h = rows_bottom_ - rows_top_;
            const float range =
                std::max(0.0f, content_height_ - std::max(view_h, 0.0f));
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            Invalidate();
            return true;
        }
        case XER::UI::Widgets::Event::Type::MouseMove: {
            const int row = RowAt(event.position);
            if (row != hovered_) {
                hovered_ = row;
                Invalidate();
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::Click: {
            const int row = RowAt(event.position);
            if (row < 0) return false;
            const auto now = std::chrono::steady_clock::now();
            const bool double_click =
                row == last_click_row_ &&
                (now - last_click_time_) < kDoubleClickWindow;
            last_click_row_ = row;
            last_click_time_ = now;
            selected_ = row;
            UpdateAddEnabled();
            EmitSelection();
            Invalidate();
            if (double_click) AddSelected();
            return true;
        }
        case XER::UI::Widgets::Event::Type::KeyDown: {
            if (event.key == XER::UI::Widgets::KeyCode::Enter) {
                AddSelected();
                return true;
            }
            if (event.key == XER::UI::Widgets::KeyCode::ArrowDown ||
                event.key == XER::UI::Widgets::KeyCode::ArrowUp) {
                if (!entries_.empty()) {
                    const int count =
                        static_cast<int>(entries_.size());
                    const int dir = event.key ==
                            XER::UI::Widgets::KeyCode::ArrowDown
                        ? 1
                        : -1;
                    // Step over section headers until a row is found.
                    int i = selected_;
                    for (int step = 0; step < count; ++step) {
                        i = i < 0 ? (dir > 0 ? 0 : count - 1)
                                  : (i + dir + count) % count;
                        if (!entries_[static_cast<std::size_t>(i)]
                                 .header) {
                            selected_ = i;
                            break;
                        }
                    }
                    if (selected_ >= 0) {
                        // Keep the selection visible.
                        const float view_h = rows_bottom_ - rows_top_;
                        const float top = EntryTop(selected_);
                        const float h = EntryHeight(selected_);
                        if (top < scroll_y_) scroll_y_ = top;
                        if (top + h > scroll_y_ + view_h) {
                            scroll_y_ = top + h - view_h;
                        }
                    }
                    UpdateAddEnabled();
                    EmitSelection();
                    Invalidate();
                }
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

void ColorPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
