#include "widget.h"

#include <algorithm>

#include "theme/tokens.h"

namespace XER::UI::Widgets {

void Widget::Layout(const Layout::Rect& bounds, const Theme::Tokens&) {
    Layout(bounds);
}

void Widget::SetVisible(bool visible) {
    visible_ = visible;
    if (!visible_) {
        ClearFocusedSubtree();
    }
}

bool Widget::IsVisible() const {
    return visible_;
}

void Widget::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        ClearFocusedSubtree();
    }
}

bool Widget::IsEnabled() const {
    return enabled_;
}

void Widget::SetFocusable(bool focusable) {
    focusable_ = focusable;
    if (!focusable_) {
        focused_ = false;
    }
}

bool Widget::IsFocusable() const {
    return focusable_;
}

void Widget::SetFocused(bool focused) {
    focused_ = focused && focusable_ && visible_ && enabled_;
}

bool Widget::IsFocused() const {
    return focused_;
}

void Widget::ClearFocusedSubtree() {
    focused_ = false;
    for (const auto& child : children_) {
        child->ClearFocusedSubtree();
    }
}

void Widget::SetParent(std::shared_ptr<Widget> parent) {
    for (auto ancestor = parent; ancestor; ancestor = ancestor->GetParent()) {
        if (ancestor.get() == this) {
            return;
        }
    }

    const auto previous = parent_.lock();
    if (previous == parent) {
        return;
    }
    if (previous) {
        auto& siblings = previous->children_;
        siblings.erase(
            std::remove_if(
                siblings.begin(),
                siblings.end(),
                [this](const std::shared_ptr<Widget>& child) { return child.get() == this; }
            ),
            siblings.end()
        );
    }

    parent_ = parent;
    if (parent) {
        const auto self = weak_from_this().lock();
        if (self != nullptr &&
            std::none_of(
                parent->children_.begin(),
                parent->children_.end(),
                [this](const std::shared_ptr<Widget>& child) { return child.get() == this; }
            )) {
            parent->children_.push_back(self);
        }
    }
}

std::shared_ptr<Widget> Widget::GetParent() const {
    return parent_.lock();
}

void Widget::AddChild(std::shared_ptr<Widget> child) {
    if (child == nullptr || child.get() == this) {
        return;
    }

    const auto self = weak_from_this().lock();
    if (self == nullptr) {
        return;
    }
    for (auto ancestor = self; ancestor; ancestor = ancestor->GetParent()) {
        if (ancestor == child) {
            return;
        }
    }
    child->SetParent(self);
}

void Widget::RemoveChild(std::shared_ptr<Widget> child) {
    if (child != nullptr && child->GetParent().get() == this) {
        child->SetParent(nullptr);
    }
}

const std::vector<std::shared_ptr<Widget>>& Widget::GetChildren() const {
    return children_;
}

Layout::Rect Widget::GetBounds() const {
    return bounds_;
}

void Widget::SetBounds(const Layout::Rect& bounds) {
    bounds_ = bounds;
}

void Widget::Invalidate() {}

void Widget::RequestLayout() {}

} // namespace XER::UI::Widgets
