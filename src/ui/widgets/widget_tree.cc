#include "widget_tree.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/tooltip.h"

namespace XER::UI::Widgets {

void WidgetTree::SetRoot(std::shared_ptr<Widget> root) {
    ClearFocus(root_);
    hovered_widget_.reset();
    root_ = std::move(root);
    if (root_ != nullptr) {
        root_->SetTreeRecursive(this);
    }
}

void WidgetTree::ShowOverlay(std::shared_ptr<Widget> overlay) {
    HideTooltip();
    ClearFocus(overlay_);
    overlay_ = std::move(overlay);
    if (overlay_ != nullptr) {
        overlay_->SetTreeRecursive(this);
        overlay_->SetBounds(last_bounds_);
        overlay_->Layout(last_bounds_);
        // Move focus into the overlay so keyboard input (Escape etc.) works.
        // Prefer focusable descendants; fall back to the overlay root itself.
        std::vector<std::shared_ptr<Widget>> focusable;
        for (const auto& child : overlay_->GetChildren()) {
            CollectFocusable(child, true, focusable);
        }
        if (focusable.empty()) {
            CollectFocusable(overlay_, true, focusable);
        }
        if (!focusable.empty()) {
            SetFocusedWidget(focusable.front());
        }
    }
}

void WidgetTree::HideOverlay() {
    ClearFocus(overlay_);
    hovered_widget_.reset();
    overlay_.reset();
}

std::shared_ptr<Widget> WidgetTree::GetRoot() const {
    return root_;
}

void WidgetTree::Layout(const Layout::Rect& bounds) {
    last_bounds_ = bounds;
    if (root_ != nullptr) {
        root_->SetBounds(bounds);
        root_->Layout(bounds);
    }
    if (overlay_ != nullptr) {
        overlay_->SetBounds(bounds);
        overlay_->Layout(bounds);
    }
    if (passive_overlay_ != nullptr) {
        passive_overlay_->SetBounds(bounds);
        passive_overlay_->Layout(bounds);
    }
}

void WidgetTree::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    last_bounds_ = bounds;
    if (root_ != nullptr) {
        root_->SetBounds(bounds);
        root_->Layout(bounds, tokens);
    }
    if (overlay_ != nullptr) {
        overlay_->SetBounds(bounds);
        overlay_->Layout(bounds, tokens);
    }
    if (passive_overlay_ != nullptr) {
        passive_overlay_->SetBounds(bounds);
        passive_overlay_->Layout(bounds, tokens);
    }
}

void WidgetTree::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) const {
    if (root_ == nullptr || !root_->IsVisible()) {
        return;
    }

    renderer.BeginFrame(root_->GetBounds());
    RenderSubtree(root_, renderer, tokens);
    if (overlay_ != nullptr) {
        RenderSubtree(overlay_, renderer, tokens);
    }
    if (passive_overlay_ != nullptr) {
        RenderSubtree(passive_overlay_, renderer, tokens);
    }
    renderer.EndFrame();
}

bool WidgetTree::HandleEvent(const Event& event) {
    if (root_ == nullptr) {
        return false;
    }
    // Presses, wheel and keys dismiss a visible tooltip immediately.
    if (event.type == Event::Type::MouseDown ||
        event.type == Event::Type::Click ||
        event.type == Event::Type::MouseWheel ||
        event.type == Event::Type::KeyDown) {
        HideTooltip();
    }
    // While an overlay is shown it owns all pointer input (modal semantics);
    // the overlay widget itself decides whether to dismiss on outside clicks.
    const auto& input_root = overlay_ != nullptr ? overlay_ : root_;
    if (suppress_duplicate_click_ && event.type != Event::Type::Click) {
        suppress_duplicate_click_ = false;
        synthesized_click_target_.reset();
    }

    if (event.type == Event::Type::KeyDown && event.key == KeyCode::Tab) {
        return MoveFocus(event.is_shift);
    }

    switch (event.type) {
    case Event::Type::MouseWheel: {
        auto target = FindHitTarget(input_root, event.position, true);
        // Wheel events bubble up the parent chain so scroll containers can
        // react even when a leaf widget (slider, button) is under the cursor.
        while (target != nullptr) {
            if (target->HandleEvent(event)) {
                return true;
            }
            target = target->GetParent();
        }
        return false;
    }
    case Event::Type::MouseMove: {
        // While the left button is held, mouse messages stay routed to the
        // widget that received MouseDown (mouse-capture semantics), which is
        // what makes slider dragging work outside the widget bounds.
        const auto captured = left_mouse_down_target_.lock();
        const auto target = captured != nullptr
            ? captured
            : FindHitTarget(input_root, event.position, true);
        if (captured == nullptr) {
            UpdateHover(target);
        }
        if (target == nullptr) {
            return false;
        }
        return target->HandleEvent(event);
    }
    case Event::Type::Click: {
        const auto target = FindHitTarget(input_root, event.position, true);
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
        if (target->IsFocusable()) {
            SetFocusedWidget(target);
        }
        return target->HandleEvent(event);
    }
    case Event::Type::MouseDown: {
        const auto target = FindHitTarget(input_root, event.position, true);
        if (event.mouse_button == MouseButton::Left) {
            left_mouse_down_target_ = target;
        }
        UpdateHover(target);
        if (target == nullptr) {
            if (event.mouse_button == MouseButton::Right && context_menu_handler_) {
                return context_menu_handler_(event);
            }
            return false;
        }
        if (target->IsFocusable()) {
            SetFocusedWidget(target);
        }
        const bool handled = target->HandleEvent(event);
        // Right-clicks that no widget consumed bubble to the tree-level
        // context-menu handler (e.g. AudioPlayerWidget's menu).
        if (!handled && event.mouse_button == MouseButton::Right &&
            context_menu_handler_) {
            return context_menu_handler_(event);
        }
        return handled;
    }
    case Event::Type::MouseUp: {
        const auto hit = FindHitTarget(input_root, event.position, true);
        const auto pressed_target = left_mouse_down_target_.lock();
        if (event.mouse_button == MouseButton::Left) {
            left_mouse_down_target_.reset();
        }
        const auto target = event.mouse_button == MouseButton::Left &&
                pressed_target != nullptr
            ? pressed_target
            : hit;
        const bool released = target != nullptr && target->HandleEvent(event);
        if (event.mouse_button != MouseButton::Left ||
            hit == nullptr || hit != pressed_target) {
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
    CollectFocusable(input_root, true, focusable);
    const auto focused = std::find_if(
        focusable.begin(),
        focusable.end(),
        [](const std::shared_ptr<Widget>& widget) { return widget->IsFocused(); }
    );
    if (focused != focusable.end() && (*focused)->HandleEvent(event)) {
        return true;
    }
    // Give the overlay root a last shot at keys the focused widget ignored
    // (e.g. Escape closing a Dialog while a child input has focus).
    if (overlay_ != nullptr && overlay_->HandleEvent(event)) {
        return true;
    }
    // Global shortcuts: only reached when nothing focused consumed the key,
    // so typing in a TextInput never triggers transport shortcuts.
    if (key_handler_ != nullptr &&
        (event.type == Event::Type::KeyDown || event.type == Event::Type::KeyUp ||
         event.type == Event::Type::TextInput)) {
        return key_handler_(event);
    }
    return false;
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
    const bool clips = widget->clips_children_;
    if (clips) {
        renderer.PushClip(widget->bounds_);
    }
    for (const auto& child : widget->GetChildren()) {
        RenderSubtree(child, renderer, tokens);
    }
    if (clips) {
        renderer.PopClip();
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

    // Clipping containers (scroll views) reject input outside their bounds
    // before descending into children whose layout rects may overflow.
    if (widget->clips_children_ && !widget->bounds_.Contains(position)) {
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
    const auto& focus_root = overlay_ != nullptr ? overlay_ : root_;
    CollectFocusable(focus_root, true, focusable);
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
    ClearFocus(overlay_);
    if (widget != nullptr) {
        widget->SetFocused(true);
    }
}

bool WidgetTree::ConsumeDirtyPaint() {
    const bool root_dirty = root_ != nullptr && root_->paint_dirty_;
    const bool overlay_dirty = overlay_ != nullptr && overlay_->paint_dirty_;
    const bool passive_dirty =
        passive_overlay_ != nullptr && passive_overlay_->paint_dirty_;
    if (!root_dirty && !overlay_dirty && !passive_dirty) {
        return false;
    }
    ClearPaintDirty(root_);
    ClearPaintDirty(overlay_);
    ClearPaintDirty(passive_overlay_);
    return true;
}

bool WidgetTree::ConsumeLayoutRequest() {
    const bool root_dirty = root_ != nullptr && root_->layout_dirty_;
    const bool overlay_dirty = overlay_ != nullptr && overlay_->layout_dirty_;
    const bool passive_dirty =
        passive_overlay_ != nullptr && passive_overlay_->layout_dirty_;
    if (!root_dirty && !overlay_dirty && !passive_dirty) {
        return false;
    }
    ClearLayoutDirty(root_);
    ClearLayoutDirty(overlay_);
    ClearLayoutDirty(passive_overlay_);
    return true;
}

void WidgetTree::ClearPaintDirty(const std::shared_ptr<Widget>& widget) {
    if (widget == nullptr) {
        return;
    }
    widget->paint_dirty_ = false;
    for (const auto& child : widget->GetChildren()) {
        ClearPaintDirty(child);
    }
}

void WidgetTree::ClearLayoutDirty(const std::shared_ptr<Widget>& widget) {
    if (widget == nullptr) {
        return;
    }
    widget->layout_dirty_ = false;
    for (const auto& child : widget->GetChildren()) {
        ClearLayoutDirty(child);
    }
}

void WidgetTree::ClearHover() {
    UpdateHover(nullptr);
}

void WidgetTree::UpdateHover(const std::shared_ptr<Widget>& target) {
    const auto current = hovered_widget_.lock();
    if (current == target) {
        return;
    }
    if (current != nullptr) {
        current->SetHovered(false);
    }
    if (target != nullptr) {
        target->SetHovered(true);
    }
    hovered_widget_ = target;
    // Hover moved: restart the tooltip delay; dismiss a tooltip that was
    // anchored to a different widget.
    hover_time_ = 0.0;
    if (tooltip_shown_ && tooltip_target_.lock() != target) {
        HideTooltip();
    }
}

void WidgetTree::HideTooltip() {
    tooltip_shown_ = false;
    tooltip_target_.reset();
    hover_time_ = 0.0;
    passive_overlay_.reset();
}

void WidgetTree::ShowTooltipFor(const std::shared_ptr<Widget>& target) {
    if (target == nullptr || target->GetTooltip().empty()) {
        return;
    }
    auto tip = Tooltip::Create(target->GetTooltip());
    tip->SetAnchor(target->GetBounds());
    tip->SetTreeRecursive(this);
    tip->SetBounds(last_bounds_);
    tip->Layout(last_bounds_);
    passive_overlay_ = std::move(tip);
    tooltip_target_ = target;
    tooltip_shown_ = true;
}

void WidgetTree::Tick(double dt_seconds) {
    if (tooltip_shown_) {
        const auto target = tooltip_target_.lock();
        const bool still_valid =
            target != nullptr && !target->GetTooltip().empty() &&
            target->IsEnabled() && target->IsEffectivelyVisible() &&
            overlay_ == nullptr;
        if (still_valid) {
            // Track the anchor widget (e.g. while a ScrollArea moves it).
            if (auto tip =
                    std::dynamic_pointer_cast<Tooltip>(passive_overlay_)) {
                tip->SetAnchor(target->GetBounds());
            }
            if (passive_overlay_ != nullptr) {
                passive_overlay_->TickTree(dt_seconds);
            }
            return;
        }
        HideTooltip();
    }
    if (overlay_ != nullptr) {
        return; // modal layer owns interaction; no hover tooltips meanwhile
    }
    const auto target = hovered_widget_.lock();
    if (target == nullptr || target->GetTooltip().empty() ||
        !target->IsEnabled() || !target->IsEffectivelyVisible() ||
        left_mouse_down_target_.lock() != nullptr) {
        hover_time_ = 0.0;
        return;
    }
    hover_time_ += dt_seconds;
    if (hover_time_ >= 0.6) {
        ShowTooltipFor(target);
    }
}

} // namespace XER::UI::Widgets
