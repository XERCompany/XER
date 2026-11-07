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

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

// Full-window overlay shown while the dropdown is open: a transparent layer
// that renders the option panel under the select field and dismisses on any
// click outside the panel.
class SelectOverlay final : public Widget {
public:
    SelectOverlay(Layout::Rect anchor, std::vector<std::string> options, int selected)
        : anchor_(anchor)
        , options_(std::move(options))
        , selected_(selected) {
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
        open_t_.Update(static_cast<float>(dt_seconds));
        if (open_t_.Active()) {
            Invalidate();
        }
    }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override {
        const float p = open_t_.Value();
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
        Theme::Color panel_bg = tokens.surface;
        panel_bg.a *= 0.96f;
        renderer.FillRect(panel, FA(panel_bg), tokens.radius_medium);
        renderer.StrokeRect(panel, FA(tokens.border), 1.0f, tokens.radius_medium);

        for (std::size_t i = 0; i < options_.size(); ++i) {
            const Layout::Rect row{panel.x, panel.y + kRowHeight * static_cast<float>(i), panel.width, kRowHeight};
            const bool active = static_cast<int>(i) == selected_;
            const bool hovered = static_cast<int>(i) == hovered_;
            if (hovered && IsEnabled()) {
                renderer.FillRect(row, FA(tokens.hover), tokens.radius_small);
            }
            const Rendering::TextStyle style{
                FA(tokens.foreground),
                tokens.font_body,
                active ? 500 : 400
            };
            const float text_x = row.x + tokens.spacing_medium +
                (active ? 18.0f : 0.0f);
            if (active) {
                // shadcn select: accent check on the left of the chosen item.
                const Rendering::TextStyle check_style{FA(tokens.accent),
                                                       tokens.font_body, 600};
                renderer.DrawText("✓",
                                  Layout::Rect{row.x + tokens.spacing_small,
                                               row.y, 16.0f, kRowHeight},
                                  check_style);
            }
            renderer.DrawText(
                options_[i],
                Layout::Rect{text_x, row.y,
                             row.x + row.width - text_x - tokens.spacing_medium,
                             kRowHeight},
                style
            );
        }
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        row_rects_.clear();
        const auto panel = PanelRect();
        for (std::size_t i = 0; i < options_.size(); ++i) {
            row_rects_.push_back(Layout::Rect{panel.x, panel.y + kRowHeight * static_cast<float>(i), panel.width, kRowHeight});
        }
    }

    bool HandleEvent(const Event& event) override {
        switch (event.type) {
            case Event::Type::MouseMove: {
                hovered_ = HitRow(event.position);
                Invalidate();
                return hovered_ >= 0;
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
                if (event.key == KeyCode::Escape && on_dismiss_) {
                    on_dismiss_();
                    return true;
                }
                return false;
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
        selected_ = selected;
        Invalidate();
        RequestLayout();
    }

private:
    static constexpr float kRowHeight = 32.0f;

    Layout::Rect PanelRect() const {
        const auto bounds = GetBounds();
        const float height = kRowHeight * static_cast<float>(options_.size()) + 8.0f;
        float y = anchor_.y + anchor_.height + 4.0f;
        if (y + height > bounds.y + bounds.height) {
            y = anchor_.y - height - 4.0f;
        }
        y = std::max(bounds.y + 4.0f, y);
        return Layout::Rect{anchor_.x, y, anchor_.width, height};
    }

    int HitRow(const Layout::Point& position) const {
        for (std::size_t i = 0; i < row_rects_.size(); ++i) {
            if (row_rects_[i].Contains(position)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    Layout::Rect anchor_;
    std::vector<std::string> options_;
    std::vector<Layout::Rect> row_rects_;
    int selected_ = -1;
    int hovered_ = -1;
    Motion::Tween open_t_;
    std::function<void(int)> on_select_;
    std::function<void()> on_dismiss_;
};

} // namespace

Select::Select(std::vector<std::string> options, int selected)
    : options_(std::move(options))
    , selected_index_(selected) {
    SetFocusable(true);
}

void Select::SetOptions(std::vector<std::string> options) {
    options_ = std::move(options);
    selected_index_ = std::clamp(selected_index_, -1, static_cast<int>(options_.size()) - 1);
    Invalidate();
    RequestLayout();
}

void Select::SetSelectedIndex(int index) {
    index = std::clamp(index, -1, static_cast<int>(options_.size()) - 1);
    if (selected_index_ == index) {
        return;
    }
    selected_index_ = index;
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
    const float text_h = renderer.MeasureText(label, text_style).height;
    renderer.DrawText(
        label,
        Layout::Rect{
            bounds.x + tokens.spacing_medium,
            bounds.y + (bounds.height - text_h) * 0.5f,
            bounds.width - 40.0f,
            text_h
        },
        text_style
    );

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
}

bool Select::HandleEvent(const Event& event) {
    if (!IsEnabled() || options_.empty()) {
        return false;
    }
    if (event.type == Event::Type::Click) {
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
    if (event.type == Event::Type::KeyDown &&
        (event.key == KeyCode::Enter || event.key == KeyCode::Space)) {
        HandleEvent(Event{Event::Type::Click, GetBounds().TopLeft(), MouseButton::Left});
        return true;
    }
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
    Invalidate();
    ShowOverlay(overlay);
}

void Select::CloseDropdown() {
    if (!open_) {
        return;
    }
    open_ = false;
    Invalidate();
    HideOverlay();
}

bool Select::IsDropdownOpen() const {
    return open_;
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
