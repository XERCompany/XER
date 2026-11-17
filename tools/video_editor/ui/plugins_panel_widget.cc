#include "plugins_panel_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "app_runtime/icon_pack.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Theme::Color;

constexpr float kRowHeight = 38.0f;
constexpr float kHeaderHeight = 24.0f;
constexpr float kHeaderIcon = 14.0f;
constexpr float kRowIcon = 16.0f;
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// Theme float color → packed AARRGGBB tint for the icon pack.
std::uint32_t ToArgb(Color color) {
    const auto byte = [](float v) {
        return static_cast<std::uint32_t>(
            std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    return (byte(color.a) << 24) | (byte(color.r) << 16) |
           (byte(color.g) << 8) | byte(color.b);
}

// Rasterized SVG icon from the shared pack, drawn centered in `box`.
// Returns false when the icon is missing or the renderer can't blit
// it — the caller then keeps the text-only fallback layout.
bool DrawIcon(XER::UI::Rendering::Renderer& renderer, const char* name,
              int px, Color tint, const XER::UI::Layout::Rect& box) {
    const auto* bmp =
        AppRuntime::SharedIconPack().Get(name, px, ToArgb(tint));
    if (bmp == nullptr) return false;
    const XER::UI::Layout::Rect dst{
        box.x + (box.width - bmp->width) * 0.5f,
        box.y + (box.height - bmp->height) * 0.5f,
        static_cast<float>(bmp->width),
        static_cast<float>(bmp->height)};
    return renderer.DrawFrameBgra(bmp->bgra.data(), bmp->width,
                                  bmp->height, dst, 1.0f);
}

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
    append_kind(XER_PLUGIN_VIDEO_FX, "Video Effects", "nav.fx");
    append_kind(XER_PLUGIN_AUDIO_FX, "Audio Effects", "nav.audio");

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
            {bounds.x + 12.0f,
             std::max(rows_top_, bounds.y + 40.0f) + 6.0f,
             bounds.width - 24.0f, 20.0f},
            XER::UI::Rendering::TextStyle{tokens.muted, tokens.font_small,
                                          400});
        return;
    }

    // Rows are drawn (not child widgets) so clicks land on the panel —
    // widget-tree Click events do not bubble up from Label children.
    if (rows_top_ <= 0.0f) return;   // not yet laid out
    const float width = bounds.width - 24.0f;
    float y = rows_top_ - scroll_y_;
    // Icon of the section currently being walked — headers carry it in
    // `tag`; rows reuse it so a group and its items share a glyph.
    const char* section_icon = nullptr;
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
            float label_x = bounds.x + 12.0f;
            if (!entry.tag.empty()) {
                section_icon = entry.tag.c_str();
                if (DrawIcon(renderer, section_icon,
                             static_cast<int>(kHeaderIcon), tokens.muted,
                             {label_x,
                              y + (kHeaderHeight - kHeaderIcon) * 0.5f,
                              kHeaderIcon, kHeaderIcon})) {
                    label_x += kHeaderIcon + 6.0f;
                }
            }
            renderer.DrawText(entry.text,
                              {label_x, y + 7.0f,
                               bounds.x + bounds.width - label_x - 12.0f,
                               14.0f},
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
            float text_x = row.x + 8.0f;
            const char* row_icon =
                section_icon != nullptr ? section_icon : "tab.plugins";
            if (DrawIcon(renderer, row_icon, static_cast<int>(kRowIcon),
                         tokens.foreground,
                         {row.x + 6.0f,
                          row.y + (row.height - kRowIcon) * 0.5f,
                          kRowIcon, kRowIcon})) {
                text_x = row.x + 6.0f + kRowIcon + 8.0f;
            }
            renderer.DrawText(entry.text,
                              {text_x, row.y + 3.0f,
                               row.x + row.width - text_x - 8.0f, 16.0f},
                              XER::UI::Rendering::TextStyle{
                                  tokens.foreground, tokens.font_body,
                                  500});
            renderer.DrawText(entry.tag,
                              {text_x, row.y + 20.0f,
                               row.x + row.width - text_x - 8.0f, 14.0f},
                              XER::UI::Rendering::TextStyle{
                                  tokens.muted, tokens.font_small, 400});
        }
        y += h;
    }
}

void PluginsPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f},
                   tokens);
    rows_top_ = bounds.y + 34.0f + gap;
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
