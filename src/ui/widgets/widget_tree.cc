#include "widget_tree.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

void WidgetTree::SetRoot(std::shared_ptr<Widget> root) {
    ClearFocus(root_);
    root_ = std::move(root);
}

std::shared_ptr<Widget> WidgetTree::GetRoot() const {
    return root_;
}

void WidgetTree::Layout(const Layout::Rect& bounds) {
    if (root_ == nullptr) {
        return;
    }

    root_->SetBounds(bounds);
    root_->Layout(bounds);
}

void WidgetTree::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    if (root_ == nullptr) {
        return;
    }

    root_->SetBounds(bounds);
    root_->Layout(bounds, tokens);
}

void WidgetTree::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) const {
    if (root_ == nullptr || !root_->IsVisible()) {
        return;
    }

    renderer.BeginFrame(root_->GetBounds());
    RenderSubtree(root_, renderer, tokens);
    renderer.EndFrame();
}

bool WidgetTree::HandleEvent(const Event& event) {
    if (root_ == nullptr) {
        return false;
    }
    if (suppress_duplicate_click_ && event.type != Event::Type::Click) {
        suppress_duplicate_click_ = false;
        synthesized_click_target_.reset();
    }

    if (event.type == Event::Type::KeyDown && event.key == KeyCode::Tab) {
        return MoveFocus(event.is_shift);
    }

    switch (event.type) {
    case Event::Type::MouseMove:
    case Event::Type::Click: {
        const auto target = FindHitTarget(root_, event.position, true);
        if (suppress_duplicate_click_) {
            const auto synthesized_target = synthesized_click_target_.lock();
            const bool duplicate = event.mouse_button == MouseButton::Left &&
                target == synthesized_target &&
                event.position.x == synthesized_click_position_.x &&
                event.position.y == synthesized_click_position_.y;
            suppress_duplicate_click_ = false;
            synthesized_click_target_.reset();
            if (duplicate) {
                return true;
            }
        }
        if (target == nullptr) {
            return false;
        }
        if ((event.type == Event::Type::MouseDown || event.type == Event::Type::Click) &&
            target->IsFocusable()) {
            SetFocusedWidget(target);
        }
        return target->HandleEvent(event);
    }
    case Event::Type::MouseDown: {
        const auto target = FindHitTarget(root_, event.position, true);
        if (event.mouse_button == MouseButton::Left) {
            left_mouse_down_target_ = target;
        }
        if (target == nullptr) {
            return false;
        }
        if (target->IsFocusable()) {
            SetFocusedWidget(target);
        }
        return target->HandleEvent(event);
    }
    case Event::Type::MouseUp: {
        const auto target = FindHitTarget(root_, event.position, true);
        const auto pressed_target = left_mouse_down_target_.lock();
        if (event.mouse_button == MouseButton::Left) {
            left_mouse_down_target_.reset();
        }
        const bool released = target != nullptr && target->HandleEvent(event);
        if (event.mouse_button != MouseButton::Left ||
            target == nullptr || target != pressed_target) {
            return released;
        }

        Event click = event;
        click.type = Event::Type::Click;
        suppress_duplicate_click_ = true;
        synthesized_click_target_ = target;
        synthesized_click_position_ = event.position;
        return target->HandleEvent(click) || released;
    }
    case Event::Type::KeyDown:
    case Event::Type::KeyUp:
    case Event::Type::TextInput:
    case Event::Type::Focus:
    case Event::Type::Blur:
        break;
    }

    std::vector<std::shared_ptr<Widget>> focusable;
    CollectFocusable(root_, true, focusable);
    const auto focused = std::find_if(
        focusable.begin(),
        focusable.end(),
        [](const std::shared_ptr<Widget>& widget) { return widget->IsFocused(); }
    );
    return focused != focusable.end() && (*focused)->HandleEvent(event);
}

void WidgetTree::ClearFocus(const std::shared_ptr<Widget>& widget) {
    if (widget == nullptr) {
        return;
    }
    widget->SetFocused(false);
    for (const auto& child : widget->GetChildren()) {
        ClearFocus(child);
    }
}

void WidgetTree::RenderSubtree(
    const std::shared_ptr<Widget>& widget,
    Rendering::Renderer& renderer,
    const Theme::Tokens& tokens
) const {
    if (widget == nullptr || !widget->IsVisible()) {
        return;
    }

    widget->Render(renderer, tokens);
    for (const auto& child : widget->GetChildren()) {
        RenderSubtree(child, renderer, tokens);
    }
}

void WidgetTree::CollectFocusable(
    const std::shared_ptr<Widget>& widget,
    bool ancestors_available,
    std::vector<std::shared_ptr<Widget>>& widgets
) const {
    if (widget == nullptr || !ancestors_available ||
        !widget->IsVisible() || !widget->IsEnabled()) {
        return;
    }

    if (widget->IsFocusable()) {
        widgets.push_back(widget);
    }
    for (const auto& child : widget->GetChildren()) {
        CollectFocusable(child, true, widgets);
    }
}

std::shared_ptr<Widget> WidgetTree::FindHitTarget(
    const std::shared_ptr<Widget>& widget,
    const Layout::Point& position,
    bool ancestors_available
) const {
    if (widget == nullptr || !ancestors_available ||
        !widget->IsVisible() || !widget->IsEnabled()) {
        return nullptr;
    }

    const auto& children = widget->GetChildren();
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
        if (auto hit = FindHitTarget(*child, position, true)) {
            return hit;
        }
    }
    return widget->bounds_.Contains(position) ? widget : nullptr;
}

bool WidgetTree::MoveFocus(bool backwards) {
    std::vector<std::shared_ptr<Widget>> focusable;
    CollectFocusable(root_, true, focusable);
    if (focusable.empty()) {
        return false;
    }

    const auto current = std::find_if(
        focusable.begin(),
        focusable.end(),
        [](const std::shared_ptr<Widget>& widget) { return widget->IsFocused(); }
    );

    std::size_t next = 0;
    if (current == focusable.end()) {
        next = backwards ? focusable.size() - 1 : 0;
    } else {
        const auto index = static_cast<std::size_t>(std::distance(focusable.begin(), current));
        next = backwards
            ? (index + focusable.size() - 1) % focusable.size()
            : (index + 1) % focusable.size();
    }
    SetFocusedWidget(focusable[next]);
    return true;
}

void WidgetTree::SetFocusedWidget(const std::shared_ptr<Widget>& widget) {
    ClearFocus(root_);
    if (widget != nullptr) {
        widget->SetFocused(true);
    }
}

} // namespace XER::UI::Widgets
