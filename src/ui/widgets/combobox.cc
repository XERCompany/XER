#include "combobox.h"

#include <algorithm>
#include <cctype>
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
}

void Combobox::CloseDropdown() {
    open_ = false;
    Invalidate();
}

void Combobox::SelectIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(filtered_.size())) return;
    const auto& opt = options_[filtered_[idx]];
    text_ = opt.label;
    value_ = opt.value.empty() ? opt.label : opt.value;
    open_ = false;
    if (on_change_) on_change_(value_);
    if (on_select_) on_select_(opt);
    Invalidate();
}

void Combobox::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (open_) {
        const float item_h = 26.0f;
        const int shown = std::min<int>(8, static_cast<int>(filtered_.size()));
        const float h = std::min(item_h * shown + 8.0f, 240.0f);
        dropdown_rect_ = {bounds.x, bounds.y + bounds.height + 4.0f,
                          bounds.width, h};
    }
}

void Combobox::Render(Rendering::Renderer& renderer,
                      const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    const bool focused = IsFocused();
    // Input box.
    renderer.FillRect(b, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(b, focused ? tokens.focus : tokens.border, 1.0f,
                        tokens.radius_medium);
    const Rendering::TextStyle style{text_.empty() ? tokens.muted
                                                    : tokens.foreground,
                                     tokens.font_body, 400};
    Layout::Rect tr{b.x + 10.0f, b.y, b.width - 30.0f, b.height};
    renderer.DrawText(text_.empty() ? placeholder_ : text_, tr, style);
    // Chevron.
    const float cx = b.x + b.width - 16.0f;
    const float cy = b.y + b.height * 0.5f;
    const float s = 4.0f;
    const float dir = open_ ? -1.0f : 1.0f;
    renderer.DrawLine({cx - s, cy - s * dir}, {cx, cy + s * dir},
                      tokens.muted, 1.4f);
    renderer.DrawLine({cx, cy + s * dir}, {cx + s, cy - s * dir},
                      tokens.muted, 1.4f);

    // Dropdown.
    if (!open_) return;
    const auto d = dropdown_rect_;
    if (d.width <= 0.0f || d.height <= 0.0f) return;
    renderer.FillRect(d, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(d, tokens.border, 1.0f, tokens.radius_medium);
    const float item_h = 26.0f;
    for (size_t i = 0; i < filtered_.size(); ++i) {
        const float iy = d.y + 4.0f + item_h * i - scroll_;
        if (iy + item_h < d.y || iy > d.y + d.height) continue;
        const bool hi = static_cast<int>(i) == highlight_;
        if (hi) {
            renderer.FillRect({d.x + 2.0f, iy, d.width - 4.0f, item_h},
                              tokens.hover, tokens.radius_small);
        }
        const auto& opt = options_[filtered_[i]];
        const Rendering::TextStyle is{tokens.foreground, tokens.font_small,
                                      400};
        Layout::Rect ir{d.x + 10.0f, iy, d.width - 20.0f, item_h};
        renderer.DrawText(opt.label, ir, is);
    }
}

void Combobox::Tick(double) {}

bool Combobox::HandleEvent(const Event& event) {
    const auto& p = event.position;
    switch (event.type) {
        case Event::Type::Click: {
            const auto b = GetBounds();
            const bool in_box =
                p.x >= b.x && p.x < b.x + b.width &&
                p.y >= b.y && p.y < b.y + b.height;
            if (in_box) {
                open_ = !open_;
                if (open_) UpdateFilter();
                Invalidate();
                return true;
            }
            if (open_ && dropdown_rect_.width > 0.0f &&
                p.x >= dropdown_rect_.x &&
                p.x < dropdown_rect_.x + dropdown_rect_.width &&
                p.y >= dropdown_rect_.y &&
                p.y < dropdown_rect_.y + dropdown_rect_.height) {
                const int idx = static_cast<int>(
                    std::floor((p.y - dropdown_rect_.y - 4.0f) / 26.0f));
                if (idx >= 0 && idx < static_cast<int>(filtered_.size())) {
                    SelectIndex(idx);
                    return true;
                }
            }
            if (open_) CloseDropdown();
            return false;
        }
        case Event::Type::KeyDown:
            // Editing keys work whether or not the dropdown is open.
            if (event.key == KeyCode::Backspace && !text_.empty()) {
                text_.pop_back();
                UpdateFilter();
                open_ = true;
                Invalidate();
                return true;
            }
            if (event.key == KeyCode::Delete && !text_.empty()) {
                text_.pop_back();
                UpdateFilter();
                open_ = true;
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
                        Invalidate();
                    }
                    return true;
                case KeyCode::ArrowUp:
                    if (!filtered_.empty()) {
                        highlight_ =
                            (highlight_ - 1 +
                             static_cast<int>(filtered_.size())) %
                            static_cast<int>(filtered_.size());
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
                UpdateFilter();
                open_ = true;
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
