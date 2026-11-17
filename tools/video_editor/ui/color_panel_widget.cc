#include "color_panel_widget.h"

#include <algorithm>
#include <utility>

#include "app_runtime/app_runtime.h"
#include "helper/path_utils.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using ThemeColor = XER::UI::Theme::Color;
namespace Ocio = XER::VideoEditor::Color;
namespace Path = XER::Helper::Path;

constexpr float kRowHeight = 38.0f;
constexpr float kHeaderHeight = 24.0f;
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

ThemeColor WithAlpha(ThemeColor color, float alpha) {
    color.a *= alpha;
    return color;
}

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
    const auto append_group = [&](bool looks, const char* label) {
        bool headed = false;
        for (const Ocio::OcioProfile& profile : profiles) {
            if (looks != !profile.lut_path.empty()) continue;
            if (!headed) {
                entries_.push_back({true, label, {}, {}});
                headed = true;
            }
            entries_.push_back({false, profile.name,
                                TagFor(profile, cfg), profile.name});
            ++profile_count;
        }
    };
    append_group(true, "LOOKS");
    append_group(false, "TRANSFORMS");

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
    // widget-tree Click events do not bubble up from Label children.
    if (rows_top_ <= 0.0f) return;   // not yet laid out
    const float width = bounds.width - 24.0f;
    float y = rows_top_ - scroll_y_;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        const float h = EntryHeight(static_cast<int>(i));
        if (y + h < rows_top_ || y > rows_bottom_) {
            y += h;
            continue;
        }
        if (entry.header) {
            renderer.DrawText(entry.text,
                              {bounds.x + 12.0f, y + 7.0f,
                               bounds.width - 24.0f, 14.0f},
                              XER::UI::Rendering::TextStyle{
                                  tokens.muted, tokens.font_small, 600});
        } else {
            const int idx = static_cast<int>(i);
            const XER::UI::Layout::Rect row{bounds.x + 8.0f, y, width,
                                            kRowHeight - 3.0f};
            if (idx == selected_) {
                renderer.FillRect(row, WithAlpha(tokens.accent, 0.16f),
                                  tokens.radius_small);
                renderer.StrokeRect(row, WithAlpha(tokens.accent, 0.5f),
                                    1.0f, tokens.radius_small);
            } else if (idx == hovered_) {
                renderer.FillRect(row, tokens.hover, tokens.radius_small);
            }
            renderer.DrawText(entry.text,
                              {row.x + 8.0f, row.y + 3.0f,
                               row.width - 16.0f, 16.0f},
                              XER::UI::Rendering::TextStyle{
                                  tokens.foreground, tokens.font_body,
                                  500});
            renderer.DrawText(entry.tag,
                              {row.x + 8.0f, row.y + 20.0f,
                               row.width - 16.0f, 14.0f},
                              XER::UI::Rendering::TextStyle{
                                  tokens.muted, tokens.font_small, 400});
        }
        y += h;
    }
}

void ColorPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f},
                   tokens);
    ocio_state_->Layout({bounds.x + pad, bounds.y + 34.0f,
                         bounds.width - pad * 2.0f, 16.0f},
                        tokens);
    rows_top_ = bounds.y + 34.0f + 18.0f + gap;
    const float footer_y = bounds.y + bounds.height - 30.0f;
    rows_bottom_ = footer_y - gap;
    add_btn_->Layout({bounds.x + pad, footer_y, 100.0f, 26.0f}, tokens);
    status_->Layout({bounds.x + pad + 108.0f, footer_y + 2.0f,
                     std::max(40.0f, bounds.width - pad * 2.0f - 108.0f),
                     22.0f},
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
