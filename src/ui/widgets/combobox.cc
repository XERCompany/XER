#include "combobox.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Combobox> Combobox::Create() {
    return std::shared_ptr<Combobox>(new Combobox());
}

Combobox::Combobox() {
    SetFocusable(true);
}

void Combobox::SetOptions(std::vector<Option> options) {
    options_ = std::move(options);
    UpdateFilter();
    Invalidate();
}

void Combobox::SetValue(std::string value) {
    value_ = std::move(value);
    // Show the matching label if found.
    for (const auto& o : options_) {
        const std::string& v = o.value.empty() ? o.label : o.value;
        if (v == value_) {
            text_ = o.label;
            break;
        }
    }
    if (on_change_) on_change_(value_);
    Invalidate();
}

std::string Combobox::GetValue() const { return value_; }

void Combobox::SetPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    Invalidate();
}

void Combobox::SetOnChange(std::function<void(const std::string&)> cb) {
    on_change_ = std::move(cb);
}

void Combobox::SetOnSelect(std::function<void(const Option&)> cb) {
    on_select_ = std::move(cb);
}

std::vector<const Combobox::Option*> Combobox::Filtered() const {
    std::vector<const Option*> out;
    for (int i : filtered_) out.push_back(&options_[i]);
    return out;
}

void Combobox::UpdateFilter() {
    filtered_.clear();
    const std::string needle = text_;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(c));
        return s;
    };
    const std::string n = lower(needle);
    for (size_t i = 0; i < options_.size(); ++i) {
        if (n.empty() ||
            lower(options_[i].label).find(n) != std::string::npos ||
            lower(options_[i].value).find(n) != std::string::npos) {
            filtered_.push_back(static_cast<int>(i));
        }
    }
    highlight_ = filtered_.empty() ? -1 : 0;
    scroll_ = 0.0f;
    if (open_) {
        // The option count changes the popup height — and therefore the
        // hit-testable widget bounds — so geometry must be refreshed.
        RequestLayout();
    }
}

void Combobox::CloseDropdown() {
    if (!open_) {
        return;
    }
    open_ = false;
    // The popup extends well past the field's own bounds: Invalidate()
    // alone would only damage the field rect, so force a relayout (which
    // triggers a full repaint on the retained canvas).
    RequestLayout();
    Invalidate();
}

Layout::Rect Combobox::DropdownRect() const {
    const auto b = field_rect_;
    const int shown =
        std::min<int>(8, static_cast<int>(filtered_.size()));
    const float h = std::min(kItemHeight * shown + 8.0f, kMaxDropdownHeight);
    return {b.x, b.y + b.height + 4.0f, b.width, h};
}

void Combobox::EnsureHighlightVisible() {
    if (highlight_ < 0) {
        return;
    }
    const auto d = DropdownRect();
    const float view_h = std::max(0.0f, d.height - 8.0f);
    const float top = kItemHeight * static_cast<float>(highlight_);
    const float bottom = top + kItemHeight;
    if (top < scroll_) {
        scroll_ = top;
    } else if (bottom > scroll_ + view_h) {
        scroll_ = bottom - view_h;
    }
    const float max_scroll =
        std::max(0.0f, kItemHeight * static_cast<float>(filtered_.size()) -
                           view_h);
    scroll_ = std::clamp(scroll_, 0.0f, max_scroll);
}

void Combobox::SelectIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(filtered_.size())) return;
    const auto& opt = options_[filtered_[idx]];
    text_ = opt.label;
    value_ = opt.value.empty() ? opt.label : opt.value;
    open_ = false;
    if (on_change_) on_change_(value_);
    if (on_select_) on_select_(opt);
    // Bounds shrink back to the field once closed (see CloseDropdown).
    RequestLayout();
    Invalidate();
}

void Combobox::Layout(const Layout::Rect& bounds) {
    field_rect_ = bounds;
    if (open_) {
        // Grow the hit-testable area to cover the popup; the visual field
        // stays field_rect_.
        SetBounds(bounds.Union(DropdownRect()));
    } else {
        SetBounds(bounds);
    }
}

void Combobox::Render(Rendering::Renderer& renderer,
                      const Theme::Tokens& tokens) {
    const auto b = field_rect_;
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    const bool focused = IsFocused();
    const bool enabled = IsEnabled();
    const bool hovered = IsHovered() && enabled;

    // Field box: recessed input background, accent border while the
    // dropdown is open or the field has focus, brighter on hover.
    const Theme::Color border = !enabled
        ? tokens.border
        : (focused || open_)
            ? tokens.ring
            : (hovered ? tokens.muted : tokens.border);
    renderer.FillRect(b, enabled ? tokens.input : tokens.secondary,
                      tokens.radius_medium);
    renderer.StrokeRect(b, border, focused || open_ ? 1.5f : 1.0f,
                        tokens.radius_medium);

    const Theme::Color text_color = !enabled
        ? tokens.disabled
        : (text_.empty() ? tokens.muted : tokens.foreground);
    const Rendering::TextStyle style{text_color, tokens.font_body, 400};
    const std::string& shown = text_.empty() ? placeholder_ : text_;
    const auto sample = shown.empty()
        ? std::string_view("Ag") : std::string_view(shown);
    const float text_h = renderer.MeasureText(sample, style).height;
    // Clip the label so long values don't run under the chevron.
    renderer.PushClip(b);
    renderer.DrawText(shown,
                      Layout::Rect{b.x + 12.0f,
                                   b.y + std::max(0.0f, (b.height - text_h) * 0.5f),
                                   std::max(0.0f, b.width - 40.0f),
                                   text_h + 1.0f},
                      style);
    renderer.PopClip();

    // Chevron (points up while the dropdown is open).
    const float cx = b.x + b.width - 16.0f;
    const float cy = b.y + b.height * 0.5f;
    const float s = 4.0f;
    const float dir = open_ ? -1.0f : 1.0f;
    const Theme::Color chevron = enabled ? tokens.muted : tokens.disabled;
    renderer.DrawLine({cx - s, cy - s * dir}, {cx, cy + s * dir},
                      chevron, 1.4f);
    renderer.DrawLine({cx, cy + s * dir}, {cx + s, cy - s * dir},
                      chevron, 1.4f);
    // RenderFocusRing can't be used here — while the popup is open the
    // widget bounds include it, so draw the ring around the field only.
    if (IsFocused()) {
        constexpr float kRingOffset = 2.0f;
        renderer.StrokeRect(
            Layout::Rect{b.x - kRingOffset, b.y - kRingOffset,
                         b.width + 2.0f * kRingOffset,
                         b.height + 2.0f * kRingOffset},
            tokens.ring, 2.0f, tokens.radius_medium + kRingOffset);
    }

    // Dropdown panel (computed fresh so the rect stays correct even when
    // open_ toggled without a layout pass).
    if (!open_) return;
    const auto d = DropdownRect();
    if (d.width <= 0.0f || d.height <= 0.0f) return;
    renderer.DrawBackdropBlur(
        Layout::Rect{d.x - 6.0f, d.y - 6.0f, d.width + 12.0f,
                     d.height + 12.0f},
        0.7f);
    Theme::Color panel_bg = tokens.surface;
    panel_bg.a = 1.0f;
    renderer.FillRect(d, panel_bg, tokens.radius_medium);
    renderer.StrokeRect(d, tokens.border, 1.0f, tokens.radius_medium);

    if (filtered_.empty()) {
        const Rendering::TextStyle empty_style{tokens.muted,
                                               tokens.font_small, 400};
        const auto empty_tr = Layout::Rect{d.x + 12.0f, d.y + 4.0f,
                                           d.width - 24.0f, kItemHeight};
        renderer.DrawText("No matches", empty_tr, empty_style);
        return;
    }

    // Rows are clipped to the panel so scrolled-off items never paint
    // outside the rounded popup.
    renderer.PushClip(Layout::Rect{d.x + 1.0f, d.y + 1.0f,
                                   d.width - 2.0f, d.height - 2.0f});
    for (size_t i = 0; i < filtered_.size(); ++i) {
        const float iy = d.y + 4.0f + kItemHeight * static_cast<float>(i) -
                         scroll_;
        if (iy + kItemHeight < d.y || iy > d.y + d.height) continue;
        const bool hi = static_cast<int>(i) == highlight_;
        const auto& opt = options_[filtered_[i]];
        const std::string& opt_value =
            opt.value.empty() ? opt.label : opt.value;
        const bool is_selected = !value_.empty() && opt_value == value_;
        const Layout::Rect row{d.x + 2.0f, iy, d.width - 4.0f, kItemHeight};
        if (hi) {
            renderer.FillRect(row, tokens.hover, tokens.radius_small);
        }
        float text_x = row.x + 10.0f;
        if (is_selected) {
            const Rendering::TextStyle check_style{tokens.accent,
                                                   tokens.font_small, 600};
            renderer.DrawText("✓", Layout::Rect{row.x + 4.0f, iy + 4.0f,
                                                14.0f, kItemHeight - 8.0f},
                              check_style);
            text_x += 16.0f;
        }
        const Rendering::TextStyle is{tokens.foreground, tokens.font_small,
                                      is_selected ? 600 : 400};
        const auto sample = opt.label.empty()
            ? std::string_view("Ag") : std::string_view(opt.label);
        const float text_h = renderer.MeasureText(sample, is).height;
        Layout::Rect ir{text_x,
                        iy + std::max(0.0f, (kItemHeight - text_h) * 0.5f),
                        std::max(0.0f, row.Right() - text_x - 8.0f),
                        text_h + 1.0f};
        renderer.DrawText(opt.label, ir, is);
    }
    renderer.PopClip();
}

void Combobox::Tick(double) {}

bool Combobox::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    const auto& p = event.position;
    switch (event.type) {
        case Event::Type::Click: {
            const auto b = field_rect_;
            const bool in_box =
                p.x >= b.x && p.x < b.x + b.width &&
                p.y >= b.y && p.y < b.y + b.height;
            if (in_box) {
                open_ = !open_;
                if (open_) UpdateFilter();
                // The popup paints outside the field bounds; a plain
                // Invalidate() would only damage the field, so request a
                // relayout to force a full repaint.
                RequestLayout();
                Invalidate();
                return true;
            }
            const auto d = DropdownRect();
            if (open_ && d.width > 0.0f &&
                p.x >= d.x && p.x < d.x + d.width &&
                p.y >= d.y && p.y < d.y + d.height) {
                const int idx = static_cast<int>(
                    std::floor((p.y - d.y - 4.0f + scroll_) / kItemHeight));
                if (idx >= 0 && idx < static_cast<int>(filtered_.size())) {
                    SelectIndex(idx);
                    return true;
                }
            }
            if (open_) CloseDropdown();
            return false;
        }
        case Event::Type::MouseMove: {
            if (!open_) return false;
            const auto d = DropdownRect();
            if (p.x >= d.x && p.x < d.x + d.width &&
                p.y >= d.y && p.y < d.y + d.height) {
                const int idx = static_cast<int>(
                    std::floor((p.y - d.y - 4.0f + scroll_) / kItemHeight));
                if (idx >= 0 && idx < static_cast<int>(filtered_.size()) &&
                    idx != highlight_) {
                    highlight_ = idx;
                    Invalidate();
                }
                return true;
            }
            return false;
        }
        case Event::Type::MouseWheel: {
            if (!open_) return false;
            const auto d = DropdownRect();
            const float view_h = std::max(0.0f, d.height - 8.0f);
            const float max_scroll = std::max(
                0.0f, kItemHeight * static_cast<float>(filtered_.size()) -
                          view_h);
            if (max_scroll <= 0.0f) return false;
            // One notch scrolls three rows.
            scroll_ = std::clamp(
                scroll_ - static_cast<float>(event.wheel_delta) / 120.0f *
                              kItemHeight * 3.0f,
                0.0f, max_scroll);
            Invalidate();
            return true;
        }
        case Event::Type::KeyDown:
            // Editing keys work whether or not the dropdown is open.
            // open_ is set before UpdateFilter so the bounds grow to cover
            // the popup (UpdateFilter requests layout while open).
            if (event.key == KeyCode::Backspace && !text_.empty()) {
                text_.pop_back();
                open_ = true;
                UpdateFilter();
                Invalidate();
                return true;
            }
            if (event.key == KeyCode::Delete && !text_.empty()) {
                text_.pop_back();
                open_ = true;
                UpdateFilter();
                Invalidate();
                return true;
            }
            if (!open_) {
                if (event.key == KeyCode::ArrowDown ||
                    event.key == KeyCode::Enter) {
                    open_ = true;
                    UpdateFilter();
                    Invalidate();
                    return true;
                }
                return false;
            }
            switch (event.key) {
                case KeyCode::ArrowDown:
                    if (!filtered_.empty()) {
                        highlight_ =
                            (highlight_ + 1) %
                            static_cast<int>(filtered_.size());
                        EnsureHighlightVisible();
                        Invalidate();
                    }
                    return true;
                case KeyCode::ArrowUp:
                    if (!filtered_.empty()) {
                        highlight_ =
                            (highlight_ - 1 +
                             static_cast<int>(filtered_.size())) %
                            static_cast<int>(filtered_.size());
                        EnsureHighlightVisible();
                        Invalidate();
                    }
                    return true;
                case KeyCode::Enter:
                    SelectIndex(highlight_);
                    return true;
                case KeyCode::Escape:
                    CloseDropdown();
                    return true;
                default:
                    return false;
            }
        case Event::Type::TextInput:
            if (!event.text.empty()) {
                text_ += event.text;
                open_ = true;
                UpdateFilter();
                Invalidate();
                return true;
            }
            return false;
        default:
            return false;
    }
}

Layout::Size Combobox::GetMinSize() const {
    return {100.0f, 28.0f};
}

Layout::Size Combobox::GetPreferredSize() const {
    return {220.0f, 32.0f};
}

Layout::Size Combobox::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
