#include "shortcuts_overlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

constexpr float kCardWidth = 660.0f;
constexpr float kCardHeight = 480.0f;
constexpr float kCardPad = 22.0f;
constexpr float kRowHeight = 21.0f;
constexpr float kGroupHeaderHeight = 26.0f;
constexpr float kGroupGap = 10.0f;
constexpr float kColumnGap = 28.0f;

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

}  // namespace

const std::vector<ShortcutsOverlay::Group>&
ShortcutsOverlay::Groups() {
    // Display-only mirror of VideoEditorAppWidget::HandleEvent's
    // bindings (plus the menu shortcut labels); keep in sync there.
    static const std::vector<Group> groups = {
        {"Tools",
         {{"V", "Selection tool"},
          {"C", "Razor tool"},
          {"B", "Ripple edit tool"},
          {"N", "Rolling edit tool"},
          {"Y", "Slip tool"},
          {"U", "Slide tool"},
          {"R", "Rate stretch tool"},
          {"H", "Hand tool"},
          {"Z", "Zoom tool"}}},
        {"Playback",
         {{"Space", "Play / pause"},
          {"J", "Shuttle back 5 s"},
          {"K", "Pause"},
          {"L", "Shuttle forward 5 s"},
          {"\xe2\x86\x90 / \xe2\x86\x92",
           "Step frame (Shift: 5 s)"},
          {"Home", "Go to start"},
          {"End", "Go to end"}}},
        {"Markers",
         {{"M", "Add marker at playhead"},
          {"I", "Set work-area in"},
          {"O", "Set work-area out"}}},
        {"Edit",
         {{"Ctrl+Z", "Undo"},
          {"Ctrl+Y", "Redo (also Ctrl+Shift+Z)"},
          {"Ctrl+S", "Save (Shift: save as)"},
          {"Ctrl+O", "Open project"},
          {"Ctrl+N", "New project"},
          {"Ctrl+P", "Export MP4"},
          {"Del", "Delete clip"},
          {"Alt+Del", "Ripple-delete clip"},
          {"S", "Toggle snapping"}}},
        {"View",
         {{"F1", "Toggle this reference"}}},
    };
    return groups;
}

ShortcutsOverlay::ShortcutsOverlay(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetVisible(false);   // opened via Toggle()
}

void ShortcutsOverlay::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    title_ = std::make_shared<Widgets::Label>("Keyboard shortcuts");
    hint_ = std::make_shared<Widgets::Label>(
        "F1 or Esc to close", true);
    AddChild(title_);
    AddChild(hint_);
}

void ShortcutsOverlay::Toggle() {
    if (IsVisible()) {
        Close();
        return;
    }
    scroll_y_ = 0.0f;
    SetVisible(true);
    Invalidate();
}

void ShortcutsOverlay::Close() {
    if (!IsVisible()) return;
    SetVisible(false);
    if (hooks_.on_close) hooks_.on_close();
}

Rect ShortcutsOverlay::CardRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kCardWidth, bounds.width - 60.0f);
    const float height = std::min(kCardHeight, bounds.height - 60.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - height) * 0.5f, width, height};
}

float ShortcutsOverlay::ColumnHeight(
    const std::vector<int>& column) const {
    const auto& groups = Groups();
    float height = 0.0f;
    for (int index : column) {
        height += kGroupHeaderHeight +
                  static_cast<float>(groups[static_cast<std::size_t>(index)]
                                         .entries.size()) *
                      kRowHeight +
                  kGroupGap;
    }
    return height;
}

void ShortcutsOverlay::LayoutColumns() {
    // Greedy balance: each group joins the currently-shorter column.
    column_a_.clear();
    column_b_.clear();
    const auto& groups = Groups();
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (ColumnHeight(column_a_) <= ColumnHeight(column_b_)) {
            column_a_.push_back(static_cast<int>(i));
        } else {
            column_b_.push_back(static_cast<int>(i));
        }
    }
}

float ShortcutsOverlay::ScrollRange() const {
    const float view_h = list_bottom_ - list_top_;
    const float content =
        std::max(ColumnHeight(column_a_), ColumnHeight(column_b_));
    return std::max(0.0f, content - std::max(view_h, 0.0f));
}

void ShortcutsOverlay::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, {0.0f, 0.0f, 0.0f, 0.58f}, 0.0f);
    const auto card = CardRect();
    renderer.FillRect(card, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(card, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({card.x, card.y, 4.0f, card.height}, tokens.accent,
                      tokens.radius_large);
    if (list_top_ <= 0.0f) return;   // not yet laid out

    const auto& groups = Groups();
    const XER::UI::Rendering::TextStyle header_style{
        tokens.muted, tokens.font_small, 700};
    const XER::UI::Rendering::TextStyle key_style{
        tokens.foreground, tokens.font_small, 600};
    const XER::UI::Rendering::TextStyle action_style{
        tokens.muted, tokens.font_body, 400};

    const Rect viewport{card.x + kCardPad, list_top_,
                        card.width - kCardPad * 2.0f,
                        list_bottom_ - list_top_};
    renderer.PushClip(viewport);

    const auto draw_column = [&](const std::vector<int>& column,
                                 float x) {
        float y = list_top_ - scroll_y_;
        for (int index : column) {
            const Group& group =
                groups[static_cast<std::size_t>(index)];
            if (y + kGroupHeaderHeight > viewport.y &&
                y < viewport.y + viewport.height) {
                renderer.DrawText(
                    group.title,
                    {x, y + 4.0f, column_width_, 18.0f}, header_style);
            }
            y += kGroupHeaderHeight;
            for (const Entry& entry : group.entries) {
                if (y + kRowHeight > viewport.y &&
                    y < viewport.y + viewport.height) {
                    // Key chip sized to the shortcut text.
                    const float text_w =
                        renderer
                            .MeasureText(entry.keys, key_style)
                            .width;
                    const float chip_w =
                        std::max(26.0f, text_w + 14.0f);
                    const Rect chip{x, y + 1.0f, chip_w,
                                    kRowHeight - 3.0f};
                    renderer.FillRect(chip, tokens.secondary,
                                      tokens.radius_small);
                    renderer.StrokeRect(chip,
                                        WithAlpha(tokens.border, 0.8f),
                                        1.0f, tokens.radius_small);
                    renderer.DrawText(entry.keys,
                                      {chip.x + 7.0f, chip.y + 2.0f,
                                       chip.width - 14.0f,
                                       chip.height - 4.0f},
                                      key_style);
                    renderer.DrawText(
                        entry.action,
                        {chip.Right() + 10.0f, y + 2.0f,
                         column_width_ - chip_w - 10.0f,
                         kRowHeight - 4.0f},
                        action_style);
                }
                y += kRowHeight;
            }
            y += kGroupGap;
        }
    };
    draw_column(column_a_, viewport.x);
    draw_column(column_b_, viewport.x + column_width_ + kColumnGap);
    renderer.PopClip();

    // Scroll affordance: a thin track hint when content overflows.
    const float range = ScrollRange();
    if (range > 0.0f) {
        const float track_h = viewport.height;
        const float thumb_h =
            std::max(24.0f, track_h * viewport.height /
                                (viewport.height + range));
        const float thumb_y =
            viewport.y + (track_h - thumb_h) * (scroll_y_ / range);
        renderer.FillRect({card.Right() - 6.0f, thumb_y, 3.0f,
                           thumb_h},
                          WithAlpha(tokens.muted, 0.5f), 1.5f);
    }
}

void ShortcutsOverlay::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto card = CardRect();
    const float px = card.x + kCardPad;
    const float pw = card.width - kCardPad * 2.0f;

    title_->Layout({px, card.y + 14.0f, pw * 0.6f, 26.0f}, tokens);
    hint_->Layout({card.Right() - kCardPad - 160.0f,
                   card.y + 19.0f, 160.0f, 16.0f},
                  tokens);
    list_top_ = card.y + 52.0f;
    list_bottom_ = card.y + card.height - 14.0f;
    column_width_ = (pw - kColumnGap) * 0.5f;
    LayoutColumns();
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
}

bool ShortcutsOverlay::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    switch (event.type) {
        case Event::Type::KeyDown:
            if (event.key == XER::UI::Widgets::KeyCode::Escape) {
                Close();
            }
            return true;   // modal: swallow every key
        case Event::Type::MouseWheel: {
            if (!CardRect().Contains(event.position)) return true;
            const float range = ScrollRange();
            if (range > 0.0f) {
                scroll_y_ = std::clamp(
                    scroll_y_ -
                        static_cast<float>(event.wheel_delta) * 0.5f,
                    0.0f, range);
                Invalidate();
            }
            return true;
        }
        case Event::Type::Click:
            if (event.mouse_button == MouseButton::Left &&
                !CardRect().Contains(event.position)) {
                Close();
            }
            return true;
        // Modal: consume presses/releases/clicks (like SettingsDialog)
        // so the workspace below stays inert while the card is up.
        default:
            return event.type == Event::Type::MouseDown ||
                   event.type == Event::Type::MouseUp;
    }
}

void ShortcutsOverlay::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
