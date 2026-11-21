#include "shortcuts_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/kbd.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;
namespace Widgets = XER::UI::Widgets;

constexpr float kCardWidth = 660.0f;
constexpr float kCardHeight = 480.0f;
constexpr float kCardPad = 22.0f;

// Row/section metrics come from the VeTheme scale — the list reads as
// the inspector's sectioned list convention.
constexpr float kGroupHeaderHeight = VeTheme::kControlHeight;
constexpr float kGroupGap = VeTheme::kSpaceMd;
constexpr float kColumnGap = VeTheme::kSpaceXl;

// The shortcut table is display-only data owned by this file; the
// overlay's private Groups() mirrors it for the member API.
struct OverlayEntry {
    const char* keys;
    const char* action;
};
struct OverlayGroup {
    const char* title;
    std::vector<OverlayEntry> entries;
};

const std::vector<OverlayGroup>& OverlayGroups() {
    // Display-only mirror of VideoEditorAppWidget::HandleEvent's
    // bindings (plus the menu shortcut labels); keep in sync there.
    static const std::vector<OverlayGroup> groups = {
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
          {"J", "Shuttle reverse (repeat: faster)"},
          {"K", "Pause / stop shuttle"},
          {"L", "Shuttle forward (repeat: faster)"},
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
          {"Ctrl+D", "Default transition at junction"},
          {"Del", "Delete clip"},
          {"Alt+Del", "Ripple-delete clip"},
          {"Q", "Ripple trim head to playhead"},
          {"W", "Ripple trim tail to playhead"},
          {", / .", "Nudge selection \xc2\xb1" "1 frame"},
          {"S", "Toggle snapping"}}},
        {"View",
         {{"?", "Toggle shortcuts overlay"}}},
    };
    return groups;
}

// Finds the first direct child of `parent` of type T — the dialogs
// reach their non-member children (ButtonGroup, Field, ScrollArea)
// this way so headers stay unchanged.
template <typename T>
T* FindChild(XER::UI::Widgets::Widget* parent) {
    if (parent == nullptr) return nullptr;
    for (const auto& child : parent->GetChildren()) {
        if (auto* typed = dynamic_cast<T*>(child.get())) {
            return typed;
        }
    }
    return nullptr;
}

// The scrollable body: Kbd chip children per shortcut entry, with
// group captions and action descriptions drawn during Render from
// rects computed in Layout (the panel's drawn-header convention).
// Column balance comes from the overlay via SetColumns().
class ShortcutListContent final : public XER::UI::Widgets::Widget {
public:
    ShortcutListContent() {
        SetFocusable(false);
        std::size_t start = 0;
        for (const OverlayGroup& group : OverlayGroups()) {
            group_kbd_start_.push_back(start);
            for (const OverlayEntry& entry : group.entries) {
                kbds_.push_back(
                    std::make_shared<Widgets::Kbd>(entry.keys));
                AddChild(kbds_.back());
            }
            start += group.entries.size();
        }
    }

    // Greedy-balance result computed by the overlay (indices into
    // OverlayGroups()); SetColumns triggers a re-layout.
    void SetColumns(std::vector<int> column_a,
                    std::vector<int> column_b) {
        if (column_a_ == column_a && column_b_ == column_b) return;
        column_a_ = std::move(column_a);
        column_b_ = std::move(column_b);
        Invalidate();
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        // Group captions — the drawn-header convention: muted, small,
        // weight 600. Action text is the list's quiet body copy.
        for (const DrawnLine& line : headers_) {
            renderer.DrawText(
                line.text, line.rect,
                {tokens.muted, tokens.font_small, 600});
        }
        for (const DrawnLine& line : actions_) {
            renderer.DrawText(
                line.text, line.rect,
                {tokens.muted, tokens.font_body, 400});
        }
    }

    void Layout(const Rect& bounds) override {
        SetBounds(bounds);
        const float column_w =
            (bounds.width - kColumnGap) * 0.5f;
        headers_.clear();
        actions_.clear();
        const auto layout_column = [&](const std::vector<int>& column,
                                       float x) {
            float y = bounds.y;
            const auto& groups = OverlayGroups();
            for (int index : column) {
                const std::size_t gi = static_cast<std::size_t>(index);
                const OverlayGroup& group = groups[gi];
                headers_.push_back(
                    {{x, y + 6.0f, column_w, 18.0f}, group.title});
                y += kGroupHeaderHeight;
                std::size_t kbd_index = group_kbd_start_[gi];
                for (const OverlayEntry& entry : group.entries) {
                    const auto& kbd = kbds_[kbd_index++];
                    // Chip hugs its text; the action sits one gap to
                    // the right of the widest part of the chip.
                    const auto pref = kbd->GetPreferredSize();
                    const float chip_y =
                        y + (VeTheme::kRowHeight - pref.height) * 0.5f;
                    kbd->Layout({x, chip_y, pref.width, pref.height},
                                CachedLayoutTokens());
                    actions_.push_back(
                        {{x + pref.width + VeTheme::kSpaceMd, y + 3.0f,
                          column_w - pref.width - VeTheme::kSpaceMd,
                          VeTheme::kRowHeight - 6.0f},
                         entry.action});
                    y += VeTheme::kRowHeight;
                }
                y += kGroupGap;
            }
            return y - bounds.y - kGroupGap;
        };
        const float height_a =
            layout_column(column_a_, bounds.x);
        const float height_b =
            layout_column(column_b_, bounds.x + column_w + kColumnGap);
        content_height_ = std::max(height_a, height_b);
    }
    using Widget::Layout;

    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {0.0f, content_height_};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {0.0f, content_height_ > 0.0f ? content_height_
                                            : EstimatedHeight()};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    // ScrollArea reads this before the first Layout — report the
    // unbalanced single-column height as a safe estimate.
    float GetContentHeight() const override {
        return content_height_ > 0.0f ? content_height_
                                      : EstimatedHeight();
    }

private:
    struct DrawnLine {
        Rect rect;
        const char* text;
    };

    static float EstimatedHeight() {
        float height = 0.0f;
        for (const OverlayGroup& group : OverlayGroups()) {
            height += kGroupHeaderHeight +
                      static_cast<float>(group.entries.size()) *
                          VeTheme::kRowHeight +
                      kGroupGap;
        }
        // Two roughly balanced columns.
        return height * 0.55f;
    }

    std::vector<std::shared_ptr<Widgets::Kbd>> kbds_;
    // Flat kbds_ index of each group's first entry (the layout walks
    // groups in column order, not table order).
    std::vector<std::size_t> group_kbd_start_;
    std::vector<int> column_a_;
    std::vector<int> column_b_;
    std::vector<DrawnLine> headers_;
    std::vector<DrawnLine> actions_;
    float content_height_ = 0.0f;
};

}  // namespace

const std::vector<ShortcutsOverlay::Group>&
ShortcutsOverlay::Groups() {
    // Conversion layer over the shared file-local table (kept so the
    // private member API stays valid for the header's declaration).
    static const std::vector<Group> groups = [] {
        std::vector<Group> out;
        out.reserve(OverlayGroups().size());
        for (const OverlayGroup& source : OverlayGroups()) {
            Group converted;
            converted.title = source.title;
            converted.entries.reserve(source.entries.size());
            for (const OverlayEntry& entry : source.entries) {
                converted.entries.push_back({entry.keys, entry.action});
            }
            out.push_back(std::move(converted));
        }
        return out;
    }();
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
        "? or Esc to close", true);
    AddChild(title_);
    AddChild(hint_);
    // The scrollable list: ShortcutListContent inside a ScrollArea
    // (clips children, owns the wheel + scrollbar). Reached later via
    // FindChild — the header's member set is unchanged.
    AddChild(std::make_shared<Widgets::ScrollArea>(
        std::make_shared<ShortcutListContent>()));
}

void ShortcutsOverlay::Toggle() {
    if (IsVisible()) {
        Close();
        return;
    }
    scroll_y_ = 0.0f;
    if (auto* scroll = FindChild<Widgets::ScrollArea>(this)) {
        scroll->ScrollTo(0.0f);
    }
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
                      VeTheme::kRowHeight +
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
    renderer.FillRect(bounds, VeTheme::kScrim, 0.0f);
    // Card chrome per the panel convention: surface fill + hairline
    // border at the large radius, accent edge bar.
    const auto card = CardRect();
    renderer.FillRect(card, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(card, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({card.x, card.y, 4.0f, card.height}, tokens.accent,
                      tokens.radius_large);
    // The ScrollArea child renders the list (clipped) and its own
    // floating scrollbar.
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

    if (auto* scroll = FindChild<Widgets::ScrollArea>(this)) {
        // Feed the column balance down to the content before laying
        // the viewport out so its first pass lands correctly.
        if (auto* content = dynamic_cast<ShortcutListContent*>(
                scroll->GetContent().get())) {
            content->SetColumns(column_a_, column_b_);
        }
        scroll->Layout({px, list_top_, column_width_ * 2.0f + kColumnGap,
                        list_bottom_ - list_top_},
                       tokens);
    }
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
        case Event::Type::MouseWheel:
            // Wheel inside the list is handled by the ScrollArea
            // child via event bubbling; anything that lands directly
            // on the card/backdrop is still swallowed (modal).
            return true;
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
