#include "switch.h"

#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

Switch::Switch(bool checked) : checked_(checked) {
    SetFocusable(true);
}

bool Switch::IsChecked() const {
    return checked_;
}

void Switch::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

void Switch::SetOnChanged(std::function<void(bool)> callback) {
    on_changed_ = std::move(callback);
}

void Switch::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(
        bounds,
        checked_ ? tokens.accent : tokens.border,
        bounds.height / 2.0f
    );
    renderer.StrokeRect(
        bounds,
        IsFocused() ? tokens.focus : tokens.border,
        1.0f,
        bounds.height / 2.0f
    );

    const float diameter = bounds.height - 6.0f;
    const float thumb_x = checked_
        ? bounds.x + bounds.width - diameter - 3.0f
        : bounds.x + 3.0f;
    renderer.FillRect(
        {thumb_x, bounds.y + 3.0f, diameter, diameter},
        tokens.surface,
        diameter / 2.0f
    );
}

void Switch::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Switch::HandleEvent(const Event& event) {
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

Layout::Size Switch::GetMinSize() const {
    return {36.0f, 20.0f};
}

Layout::Size Switch::GetPreferredSize() const {
    return {40.0f, 24.0f};
}

Layout::Size Switch::GetMaxSize() const {
    return {40.0f, 24.0f};
}

void Switch::Toggle() {
    checked_ = !checked_;
    Invalidate();
    if (on_changed_) {
        on_changed_(checked_);
    }
}

} // namespace XER::UI::Widgets
