#include "plugins_panel_widget.h"

#include <algorithm>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

// Two-line catalog row: body-size name over a caption-size vendor tag.
constexpr float kRowHeight = VeTheme::kFontBody * 1.4f +
                             VeTheme::kFontCaption * 1.4f +
                             VeTheme::kSpaceXs;
constexpr float kHeaderHeight = VeTheme::kRowHeight;
constexpr float kHeaderIcon = VeTheme::kFontBody;
constexpr float kRowIcon = VeTheme::kSpaceLg;
constexpr float kActionWidth = 100.0f;   // "Add to Clip" button
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

std::string TagFor(const PluginDesc& desc) {
    if (desc.vendor.empty()) return desc.file;
    if (desc.file.empty()) return desc.vendor;
    return desc.vendor + " · " + desc.file;
}

}  // namespace

PluginsPanelWidget::PluginsPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // list scrolls inside the panel
    Reload();
}

void PluginsPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("PLUGINS");
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add to Clip", Widgets::ButtonStyle::Primary);
    add_btn_->SetTooltip("Add the selected plugin effect to the clip");
    add_btn_->SetOnClick([this] { AddSelected(); });
    status_ = std::make_shared<Widgets::Label>("Select a plugin", true);

    AddChild(title_);
    AddChild(add_btn_);
    AddChild(status_);
    status_->SetText(entries_.empty() ? "No plugins found"
                                    : "Select a plugin");
    UpdateAddEnabled();
}

void PluginsPanelWidget::Reload() {
    // Keep the selection across a rescan when its id still exists.
    const std::string keep =
        selected_ >= 0 && selected_ < static_cast<int>(entries_.size())
            ? entries_[static_cast<std::size_t>(selected_)].id
            : std::string();

    entries_.clear();
    int effect_count = 0;
    const auto& effects = SharedPluginHost().Effects();
    // Header entries stash their group icon name (assets/icons.yaml)
    // in `tag` — headers never render a tag line, and the icon names
    // double as the section icon for the rows that follow.
    const auto append_kind = [&](XER_PluginKind kind, const char* label,
                                 const char* icon) {
        bool headed = false;
        for (const PluginDesc& desc : effects) {
            if (desc.kind != kind) continue;
            if (!headed) {
                entries_.push_back({true, label, icon, {}});
                headed = true;
            }
            entries_.push_back({false, desc.name, TagFor(desc), desc.id});
            ++effect_count;
        }
    };
    // Section labels uppercase — the inspector's drawn-header style.
    append_kind(XER_PLUGIN_VIDEO_FX, "VIDEO EFFECTS", "nav.fx");
    append_kind(XER_PLUGIN_AUDIO_FX, "AUDIO EFFECTS", "nav.audio");

    content_height_ = 0.0f;
    for (const Entry& entry : entries_) {
        content_height_ +=
            entry.header ? kHeaderHeight : kRowHeight;
    }
    scroll_y_ = std::clamp(scroll_y_, 0.0f,
                           std::max(0.0f, content_height_));
    hovered_ = -1;
    last_click_row_ = -1;

    selected_ = -1;
    if (!keep.empty()) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (!entries_[i].header && entries_[i].id == keep) {
                selected_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (children_attached_) {
        UpdateAddEnabled();
        status_->SetText(effect_count == 0
                             ? "No plugins found"
                             : std::to_string(effect_count) + " effect(s)");
    }
    Invalidate();
}

void PluginsPanelWidget::UpdateAddEnabled() {
    add_btn_->SetEnabled(
        selected_ >= 0 &&
        selected_ < static_cast<int>(entries_.size()) &&
        !entries_[static_cast<std::size_t>(selected_)].header);
}

float PluginsPanelWidget::EntryHeight(int index) const {
    return entries_[static_cast<std::size_t>(index)].header
               ? kHeaderHeight
               : kRowHeight;
}

float PluginsPanelWidget::EntryTop(int index) const {
    float y = 0.0f;
    for (int i = 0; i < index; ++i) y += EntryHeight(i);
    return y;
}

int PluginsPanelWidget::RowAt(
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

void PluginsPanelWidget::AddSelected() {
    if (selected_ < 0 ||
        selected_ >= static_cast<int>(entries_.size()) ||
        entries_[static_cast<std::size_t>(selected_)].header) {
        SetStatus("Select a plugin");
        return;
    }
    const Entry& entry = entries_[static_cast<std::size_t>(selected_)];
    // Audio plugins can't ride the clip effect chain — the compositor
    // video path silently no-ops them (PluginDesc::video_fn == nullptr).
    // The Media Bin bounce workflow is the supported path for audio fx.
    if (const PluginDesc* desc = SharedPluginHost().Find(entry.id);
        desc != nullptr && desc->kind == XER_PLUGIN_AUDIO_FX) {
        SetStatus("audio fx — use Media Bin bounce instead");
        return;
    }
    if (hooks_.on_add_effect) {
        hooks_.on_add_effect("plugin:" + entry.id);
    }
    SetStatus("added " + entry.text);
}

void PluginsPanelWidget::SetStatus(const std::string& text) {
    if (children_attached_) status_->SetText(text);
}

void PluginsPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (entries_.empty()) {
        renderer.DrawText(
            "No plugins found in <exe>/plugins",
            {bounds.x + VeTheme::kSpaceMd,
             std::max(rows_top_, bounds.y + VeTheme::kSpaceMd * 3.0f) +
                 VeTheme::kSpaceXs,
             bounds.width - VeTheme::kSpaceMd * 2.0f,
             tokens.font_body * 1.4f},
            XER::UI::Rendering::TextStyle{tokens.muted, tokens.font_small,
                                          400});
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
            const char* row_icon =
                section_icon != nullptr ? section_icon : "tab.plugins";
            if (VeIcons::DrawIcon(renderer, row_icon, static_cast<int>(kRowIcon),
                         tokens.foreground,
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

void PluginsPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;
    float y = bounds.y + pad * 0.6f;

    title_->Layout({inner_x, y, inner_w, VeTheme::kRowHeight}, tokens);
    y += VeTheme::kRowHeight + gap;
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

bool PluginsPanelWidget::HandleEvent(
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

void PluginsPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
