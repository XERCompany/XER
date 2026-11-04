#include "checkbox.h"

#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Checkbox::Checkbox(bool checked) : checked_(checked) {
    SetFocusable(true);
}

bool Checkbox::IsChecked() const {
    return checked_;
}

void Checkbox::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

void Checkbox::SetOnChanged(std::function<void(bool)> callback) {
    on_changed_ = std::move(callback);
}

void Checkbox::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const Layout::Rect box{
        bounds.x,
        bounds.y + (bounds.height - 18.0f) / 2.0f,
        18.0f,
        18.0f
    };
    renderer.FillRect(box, checked_ ? tokens.accent : tokens.surface, tokens.radius_small);
    renderer.StrokeRect(
        box,
        IsFocused() ? tokens.focus : tokens.border,
        1.0f,
        tokens.radius_small
    );
    if (checked_) {
        renderer.DrawLine(
            {box.x + 4.0f, box.y + 9.0f},
            {box.x + 8.0f, box.y + 13.0f},
            tokens.background,
            2.0f
        );
        renderer.DrawLine(
            {box.x + 8.0f, box.y + 13.0f},
            {box.x + 15.0f, box.y + 5.0f},
            tokens.background,
            2.0f
        );
    }
}

void Checkbox::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Checkbox::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    if (event.type == Event::Type::Click && event.mouse_button == MouseButton::Left) {
        Toggle();
        return true;
    }
    if (event.type == Event::Type::KeyDown && IsFocused() &&
        event.key == KeyCode::Space) {
        Toggle();
        return true;
    }
    return false;
}

Layout::Size Checkbox::GetMinSize() const {
    return {18.0f, 18.0f};
}

Layout::Size Checkbox::GetPreferredSize() const {
    return {18.0f, 18.0f};
}

Layout::Size Checkbox::GetMaxSize() const {
    return {18.0f, 18.0f};
}

void Checkbox::Toggle() {
    checked_ = !checked_;
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

} // namespace XER::UI::Widgets
