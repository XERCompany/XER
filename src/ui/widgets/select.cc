#include "select.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace {

// Full-window overlay shown while the dropdown is open: a transparent layer
// that renders the option panel under the select field and dismisses on any
// click outside the panel. The panel height is capped and scrolls with the
// wheel / PageUp/PageDown so long option lists stay reachable.
class SelectOverlay final : public Widget {
public:
    SelectOverlay(Layout::Rect anchor, std::vector<std::string> options, int selected)
        : anchor_(anchor)
        , options_(std::move(options))
        , selected_(selected)
        , active_(selected) {
        SetFocusable(false);
        // Popover entrance: quick ease-out fade + 4px rise.
        open_t_.Set(1.0f, 0.14f);
    }

    void SetCallbacks(std::function<void(int)> on_select, std::function<void()> on_dismiss) {
        on_select_ = std::move(on_select);
        on_dismiss_ = std::move(on_dismiss);
    }

    void Tick(double dt_seconds) override {
        if (!IsEffectivelyVisible()) {
            return;
        }
        if (!ticked_ && rendered_) {
            // Already painted fully opaque: skip the fade to avoid a flash.
            open_t_.Set(1.0f, 0.0f);
        }
        ticked_ = true;
        open_t_.Update(static_cast<float>(dt_seconds));
        if (open_t_.Active()) {
            Invalidate();
        }
    }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override {
        // Most hosts never tick the modal overlay layer; without ticks the
        // fade-in would stay at alpha 0 and the options would be invisible.
        const float p = ticked_ ? open_t_.Value() : 1.0f;
        rendered_ = true;
        const auto FA = [p](Theme::Color col) {
            col.a *= p;
            return col;
        };
        Layout::Rect panel = PanelRect();
        panel.y += (1.0f - p) * -4.0f;
        renderer.DrawBackdropBlur(
            Layout::Rect{panel.x - 6.0f, panel.y - 6.0f, panel.width + 12.0f,
                         panel.height + 12.0f},
            0.7f);
        // Opaque panel: options must stay readable over any content below.
        Theme::Color panel_bg = tokens.surface;
        panel_bg.a = 1.0f;
        renderer.FillRect(panel, FA(panel_bg), tokens.radius_medium);
        renderer.StrokeRect(panel, FA(tokens.border), 1.0f, tokens.radius_medium);

        // Clip rows to the panel so scrolled-off options never spill out.
        renderer.PushClip(Layout::Rect{panel.x + 1.0f, panel.y + 1.0f,
                                       panel.width - 2.0f,
                                       panel.height - 2.0f});
        for (std::size_t i = 0; i < options_.size(); ++i) {
            const float row_y = panel.y + kRowHeight * static_cast<float>(i) - scroll_y_;
            if (row_y + kRowHeight < panel.y || row_y > panel.y + panel.height) {
                continue;
            }
            const Layout::Rect row{panel.x, row_y, panel.width, kRowHeight};
            const bool is_selected = static_cast<int>(i) == selected_;
            const bool is_active = static_cast<int>(i) == active_;
            if (is_active && IsEnabled()) {
                renderer.FillRect(row, FA(tokens.hover), tokens.radius_small);
            }
            const Rendering::TextStyle style{
                FA(tokens.foreground),
                tokens.font_body,
                is_selected ? 500 : 400
            };
            const float text_x = row.x + tokens.spacing_medium +
                (is_selected ? 18.0f : 0.0f);
            if (is_selected) {
                // shadcn select: accent check on the left of the chosen item.
                const Rendering::TextStyle check_style{FA(tokens.accent),
                                                       tokens.font_body, 600};
                const float check_h = renderer.MeasureText("✓", check_style).height;
                renderer.DrawText("✓",
                                  Layout::Rect{row.x + tokens.spacing_small,
                                               row.y + std::max(0.0f, (kRowHeight - check_h) * 0.5f),
                                               16.0f, check_h + 1.0f},
                                  check_style);
            }
            const auto opt_sample = options_[i].empty()
                ? std::string_view("Ag")
                : std::string_view(options_[i]);
            const float opt_h = renderer.MeasureText(opt_sample, style).height;
            renderer.DrawText(
                options_[i],
                Layout::Rect{text_x,
                             row.y + std::max(0.0f, (kRowHeight - opt_h) * 0.5f),
                             std::max(0.0f, row.x + row.width - text_x - tokens.spacing_medium),
                             opt_h + 1.0f},
                style
            );
        }
        renderer.PopClip();
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
    }

    bool HandleEvent(const Event& event) override {
        switch (event.type) {
            case Event::Type::MouseWheel:
                if (ScrollRange() > 0.0f && PanelRect().Contains(event.position)) {
                    ScrollBy(event.wheel_delta);
                    return true;
                }
                return false;
            case Event::Type::MouseMove: {
                const int row = HitRow(event.position);
                if (row >= 0 && row != active_) {
                    active_ = row;
                    EnsureActiveVisible();
                    Invalidate();
                }
                return PanelRect().Contains(event.position);
            }
            case Event::Type::Click: {
                if (PanelRect().Contains(event.position)) {
                    const int row = HitRow(event.position);
                    if (row >= 0 && on_select_) {
                        on_select_(row);
                    }
                    return true;
                }
                // Click outside the panel: dismiss without selecting.
                if (on_dismiss_) {
                    on_dismiss_();
                }
                return true; // consume: outside click only closes the popup
            }
            case Event::Type::KeyDown:
                switch (event.key) {
                    case KeyCode::Escape:
                        if (on_dismiss_) on_dismiss_();
                        return true;
                    case KeyCode::ArrowUp:
                        MoveActive(-1);
                        return true;
                    case KeyCode::ArrowDown:
                        MoveActive(1);
                        return true;
                    case KeyCode::Home:
                        SetActive(0);
                        return true;
                    case KeyCode::End:
                        SetActive(static_cast<int>(options_.size()) - 1);
                        return true;
                    case KeyCode::PageUp:
                        ScrollBy(120 * 3);
                        return true;
                    case KeyCode::PageDown:
                        ScrollBy(-120 * 3);
                        return true;
                    case KeyCode::Enter:
                    case KeyCode::Space:
                        if (active_ >= 0 && active_ < static_cast<int>(options_.size()) &&
                            on_select_) {
                            on_select_(active_);
                        }
                        return true;
                    default:
                        return false;
                }
            default:
                return false;
        }
    }

    Layout::Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetPreferredSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

    void Update(const Layout::Rect& anchor, const std::vector<std::string>& options, int selected) {
        anchor_ = anchor;
        options_ = options;
        selected_ = std::clamp(selected, -1, static_cast<int>(options_.size()) - 1);
        if (active_ < 0 || active_ >= static_cast<int>(options_.size())) {
            active_ = selected_;
        }
        scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
        EnsureActiveVisible();
        Invalidate();
        RequestLayout();
    }

private:
    static constexpr float kRowHeight = 32.0f;
    static constexpr float kMaxPanelHeight = 320.0f;

    // Total height needed for every option row.
    float ContentHeight() const {
        return kRowHeight * static_cast<float>(options_.size());
    }

    // Height of the scrollable band inside the panel (panel has 4px
    // breathing room top and bottom).
    float ViewportHeight() const {
        return std::max(0.0f, PanelHeight() - 8.0f);
    }

    float PanelHeight() const {
        return std::min(ContentHeight() + 8.0f, kMaxPanelHeight);
    }

    float ScrollRange() const {
        return std::max(0.0f, ContentHeight() - ViewportHeight());
    }

    Layout::Rect PanelRect() const {
        const auto bounds = GetBounds();
        const float height = PanelHeight();
        float y = anchor_.y + anchor_.height + 4.0f;
        const float space_below = bounds.y + bounds.height - 4.0f - y;
        if (space_below < height) {
            // Flip above the anchor when there is more room there.
            const float space_above = anchor_.y - 4.0f - bounds.y;
            if (space_above > space_below) {
                y = anchor_.y - height - 4.0f;
            }
        }
        y = std::clamp(y, bounds.y + 4.0f,
                       std::max(bounds.y + 4.0f,
                                bounds.y + bounds.height - height - 4.0f));
        return Layout::Rect{anchor_.x, y, anchor_.width, height};
    }

    int HitRow(const Layout::Point& position) const {
        const auto panel = PanelRect();
        if (!panel.Contains(position)) {
            return -1;
        }
        const float local_y = position.y - panel.y + scroll_y_;
        const int row = static_cast<int>(local_y / kRowHeight);
        if (row < 0 || row >= static_cast<int>(options_.size())) {
            return -1;
        }
        return row;
    }

    void ScrollBy(int wheel_delta) {
        // One notch ≈ 3 rows.
        scroll_y_ = std::clamp(
            scroll_y_ - static_cast<float>(wheel_delta) / 120.0f * kRowHeight * 3.0f,
            0.0f, ScrollRange());
        RequestLayout();
        Invalidate();
    }

    void MoveActive(int delta) {
        const int n = static_cast<int>(options_.size());
        if (n == 0) {
            return;
        }
        int next = active_ < 0 ? (delta > 0 ? 0 : n - 1)
                               : (active_ + delta + n) % n;
        SetActive(next);
    }

    void SetActive(int index) {
        index = std::clamp(index, -1, static_cast<int>(options_.size()) - 1);
        if (index == active_) {
            return;
        }
        active_ = index;
        EnsureActiveVisible();
        Invalidate();
    }

    void EnsureActiveVisible() {
        if (active_ < 0) {
            return;
        }
        const float top = kRowHeight * static_cast<float>(active_);
        const float bottom = top + kRowHeight;
        if (top < scroll_y_) {
            scroll_y_ = top;
        } else if (bottom > scroll_y_ + ViewportHeight()) {
            scroll_y_ = bottom - ViewportHeight();
        }
        scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
    }

    Layout::Rect anchor_;
    std::vector<std::string> options_;
    int selected_ = -1;
    int active_ = -1;
    float scroll_y_ = 0.0f;
    bool ticked_ = false;
    bool rendered_ = false;
    Motion::Tween open_t_;
    std::function<void(int)> on_select_;
    std::function<void()> on_dismiss_;
};

// Resolves the weak dropdown_ handle to the concrete overlay type.
std::shared_ptr<SelectOverlay> LockedOverlay(const std::weak_ptr<Widget>& weak) {
    return std::dynamic_pointer_cast<SelectOverlay>(weak.lock());
}

} // namespace

Select::Select(std::vector<std::string> options, int selected)
    : options_(std::move(options))
    , selected_index_(selected) {
    SetFocusable(true);
}

Select::~Select() {
    // An open dropdown is an overlay owned by the tree; if the select dies
    // first the overlay would stay modal forever with dead callbacks.
    CloseDropdown();
}

void Select::SetOptions(std::vector<std::string> options) {
    options_ = std::move(options);
    selected_index_ = std::clamp(selected_index_, -1, static_cast<int>(options_.size()) - 1);
    if (const auto overlay = LockedOverlay(dropdown_)) {
        overlay->Update(GetBounds(), options_, selected_index_);
    }
    Invalidate();
    RequestLayout();
}

void Select::SetSelectedIndex(int index) {
    index = std::clamp(index, -1, static_cast<int>(options_.size()) - 1);
    if (selected_index_ == index) {
        return;
    }
    selected_index_ = index;
    if (const auto overlay = LockedOverlay(dropdown_)) {
        overlay->Update(GetBounds(), options_, selected_index_);
    }
    Invalidate();
    if (on_changed_) {
        on_changed_(index);
    }
}

const std::string& Select::GetSelectedText() const {
    static const std::string kEmpty;
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(options_.size())) {
        return options_[static_cast<std::size_t>(selected_index_)];
    }
    return kEmpty;
}

void Select::SetPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    Invalidate();
}

void Select::SetOnChanged(std::function<void(int)> callback) {
    on_changed_ = std::move(callback);
}

void Select::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const bool hovered = IsHovered() && IsEnabled();

    Theme::Color stroke = tokens.border;
    if (IsFocused()) {
        stroke = tokens.foreground;
    } else if (hovered) {
        stroke = tokens.muted;
    }
    if (open_) {
        stroke = tokens.foreground;
    }
    renderer.FillRect(bounds, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(bounds, stroke, IsFocused() || open_ ? 1.5f : 1.0f, tokens.radius_medium);

    const bool has_value = selected_index_ >= 0 && selected_index_ < static_cast<int>(options_.size());
    const Rendering::TextStyle text_style{
        has_value ? tokens.foreground : tokens.muted,
        tokens.font_body,
        400
    };
    const std::string& label = has_value ? options_[static_cast<std::size_t>(selected_index_)] : placeholder_;
    const auto sample = label.empty() ? std::string_view("Ag") : std::string_view(label);
    const float text_h = renderer.MeasureText(sample, text_style).height;
    // Clip the label to the field so long options don't paint over the chevron.
    renderer.PushClip(bounds);
    renderer.DrawText(
        label,
        Layout::Rect{
            bounds.x + 12.0f,
            bounds.y + std::max(0.0f, (bounds.height - text_h) * 0.5f),
            std::max(0.0f, bounds.width - 34.0f),
            text_h + 1.0f
        },
        text_style
    );
    renderer.PopClip();

    // Chevron: v / ^ when open.
    const float cx = bounds.x + bounds.width - 18.0f;
    const float cy = bounds.y + bounds.height * 0.5f;
    const Theme::Color chevron = tokens.muted;
    if (open_) {
        renderer.DrawLine(Layout::Point{cx - 4.0f, cy + 2.0f}, Layout::Point{cx, cy - 2.0f}, chevron, 1.5f);
        renderer.DrawLine(Layout::Point{cx, cy - 2.0f}, Layout::Point{cx + 4.0f, cy + 2.0f}, chevron, 1.5f);
    } else {
        renderer.DrawLine(Layout::Point{cx - 4.0f, cy - 2.0f}, Layout::Point{cx, cy + 2.0f}, chevron, 1.5f);
        renderer.DrawLine(Layout::Point{cx, cy + 2.0f}, Layout::Point{cx + 4.0f, cy - 2.0f}, chevron, 1.5f);
    }
    RenderFocusRing(renderer, tokens, tokens.radius_medium);
}

void Select::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (const auto overlay = LockedOverlay(dropdown_)) {
        overlay->Update(bounds, options_, selected_index_);
    }
}

bool Select::HandleEvent(const Event& event) {
    if (!IsEnabled() || options_.empty()) {
        return false;
    }
    if (event.type == Event::Type::Click) {
        // The tree keeps only one modal overlay: a Dialog or ContextMenu
        // shown while our dropdown was open replaces it and expires
        // dropdown_. Treat that as "closed" instead of letting a stale
        // open_ flag close somebody else's overlay below.
        if (open_ && dropdown_.expired()) {
            open_ = false;
        }
        if (GetTree() != nullptr) {
            if (open_) {
                CloseDropdown();
            } else {
                OpenDropdown();
            }
        } else {
            // Detached fallback: cycle options.
            SetSelectedIndex((selected_index_ + 1) % static_cast<int>(options_.size()));
        }
        return true;
    }
    if (event.type == Event::Type::KeyDown && !open_ &&
        (event.key == KeyCode::Enter || event.key == KeyCode::Space)) {
        HandleEvent(Event{Event::Type::Click, GetBounds().TopLeft(), MouseButton::Left});
        return true;
    }
    // While the dropdown is open, keys go to the overlay (arrow nav,
    // Enter to pick, Escape to dismiss) via the tree's overlay fallback.
    return false;
}

void Select::OpenDropdown() {
    if (GetTree() == nullptr || GetBounds().width <= 0.0f) {
        return;
    }
    auto overlay = std::make_shared<SelectOverlay>(GetBounds(), options_, selected_index_);
    const auto weak_self = weak_from_this();
    overlay->SetCallbacks(
        [weak_self](int index) {
            if (const auto self = std::static_pointer_cast<Select>(weak_self.lock())) {
                self->SetSelectedIndex(index);
                self->CloseDropdown();
            }
        },
        [weak_self]() {
            if (const auto self = std::static_pointer_cast<Select>(weak_self.lock())) {
                self->CloseDropdown();
            }
        }
    );
    open_ = true;
    dropdown_ = overlay;
    Invalidate();
    ShowOverlay(overlay);
}

void Select::CloseDropdown() {
    if (!open_) {
        return;
    }
    open_ = false;
    // HideOverlay() clears whatever overlay the tree currently shows. Only
    // call it when our dropdown is still that overlay — an on_changed_
    // callback may have opened a Dialog in the meantime, and killing it
    // would be a bug.
    const auto overlay = dropdown_.lock();
    const bool ours = overlay != nullptr && GetTree() != nullptr &&
                      GetTree()->GetOverlay() == overlay;
    dropdown_.reset();
    Invalidate();
    if (ours) {
        HideOverlay();
    }
}

bool Select::IsDropdownOpen() const {
    return open_ && !dropdown_.expired();
}

Layout::Size Select::GetMinSize() const {
    return {0.0f, 36.0f};
}

Layout::Size Select::GetPreferredSize() const {
    return {160.0f, 36.0f};
}

Layout::Size Select::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
