#include "widget.h"

#include <algorithm>

#include "content/css/computed_style.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {

void Widget::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    cached_tokens_ = tokens;
    Layout(bounds);
}

void Widget::SetVisible(bool visible) {
    if (visible_ == visible) {
        return;
    }
    visible_ = visible;
    if (!visible_) {
        ClearSubtreeInteractionState();
    }
    Invalidate();
}

bool Widget::IsVisible() const {
    return visible_;
}

void Widget::SetEnabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled_) {
        ClearSubtreeInteractionState();
    }
    Invalidate();
}

bool Widget::IsEnabled() const {
    return enabled_;
}

void Widget::SetFocusable(bool focusable) {
    if (focusable_ == focusable) {
        return;
    }
    focusable_ = focusable;
    if (!focusable_) {
        focused_ = false;
    }
    Invalidate();
}

bool Widget::IsFocusable() const {
    return focusable_;
}

void Widget::SetFocused(bool focused) {
    const bool next = focused && focusable_ && visible_ && enabled_;
    if (focused_ == next) {
        return;
    }
    focused_ = next;
    Invalidate();
}

bool Widget::IsFocused() const {
    return focused_;
}

void Widget::SetHovered(bool hovered) {
    if (hovered_ == hovered) {
        return;
    }
    hovered_ = hovered;
    Invalidate();
}

bool Widget::IsHovered() const {
    return hovered_;
}

void Widget::SetActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    Invalidate();
}

// ── CSS style identity ────────────────────────────────────────────

void Widget::SetStyleId(std::string id) {
    if (style_id_ == id) {
        return;
    }
    style_id_ = std::move(id);
    Invalidate();
}

void Widget::AddStyleClass(std::string cls) {
    if (HasStyleClass(cls)) {
        return;
    }
    style_classes_.push_back(std::move(cls));
    Invalidate();
}

void Widget::RemoveStyleClass(std::string_view cls) {
    const auto it = std::find(style_classes_.begin(),
                              style_classes_.end(), cls);
    if (it == style_classes_.end()) {
        return;
    }
    style_classes_.erase(it);
    Invalidate();
}

bool Widget::HasStyleClass(std::string_view cls) const {
    return std::find(style_classes_.begin(), style_classes_.end(), cls)
           != style_classes_.end();
}

void Widget::SetStyleAttribute(std::string name, std::string value) {
    for (auto& [n, v] : style_attributes_) {
        if (n == name) {
            if (v == value) return;
            v = std::move(value);
            Invalidate();
            return;
        }
    }
    style_attributes_.emplace_back(std::move(name), std::move(value));
    Invalidate();
}

void Widget::SetInlineStyle(std::string property, std::string value) {
    for (auto& [p, v] : inline_style_) {
        if (p == property) {
            if (v == value) return;
            v = std::move(value);
            Invalidate();
            return;
        }
    }
    inline_style_.emplace_back(std::move(property), std::move(value));
    Invalidate();
}

void Widget::SetComputedStyle(
    std::shared_ptr<const Content::CSS::ComputedStyle> style) {
    computed_style_ = std::move(style);
    Invalidate();
}

const std::string* Widget::StyleValue(std::string_view property) const {
    if (!computed_style_) return nullptr;
    return computed_style_->Get(std::string(property));
}

void Widget::SetTooltip(std::string text) {
    if (tooltip_text_ == text) {
        return;
    }
    tooltip_text_ = std::move(text);
    // If this widget is being hovered, the tree re-evaluates on next Tick.
}

void Widget::SetTextDirection(TextDirection direction) {
    text_direction_ = direction;
}

TextDirection Widget::EffectiveTextDirection() const {
    // Each lock() keeps that ancestor alive for the duration of its check.
    for (const Widget* w = this; w != nullptr;) {
        if (w->text_direction_ != TextDirection::Inherit) {
            return w->text_direction_;
        }
        const auto parent = w->parent_.lock();
        w = parent.get();
    }
    return TextDirection::Ltr;
}

bool Widget::IsEffectivelyVisible() const {
    if (!visible_) {
        return false;
    }
    // Each lock() keeps that ancestor alive for the duration of its check.
    for (std::shared_ptr<Widget> p = parent_.lock(); p != nullptr;
         p = p->parent_.lock()) {
        if (!p->visible_) {
            return false;
        }
    }
    return true;
}

void Widget::SetClipsChildren(bool clips) {
    clips_children_ = clips;
}

bool Widget::ClipsChildren() const {
    return clips_children_;
}

void Widget::SetVisibleRect(const Layout::Rect& rect) {
    visible_rect_ = rect;
    has_visible_rect_ = true;
}

Layout::Rect Widget::GetVisibleRect() const {
    return has_visible_rect_ ? visible_rect_ : bounds_;
}

void Widget::SetTreeRecursive(WidgetTree* tree) {
    tree_ = tree;
    FlushPendingChildren();
    for (const auto& child : children_) {
        child->SetTreeRecursive(tree);
    }
}

void Widget::FlushPendingChildren() {
    if (pending_children_.empty()) {
        return;
    }
    const auto self = weak_from_this().lock();
    if (self == nullptr) {
        return;
    }
    auto pending = std::move(pending_children_);
    pending_children_.clear();
    for (auto& child : pending) {
        child->SetParent(self);
    }
}

void Widget::ShowOverlay(std::shared_ptr<Widget> overlay) {
    if (tree_ != nullptr) {
        tree_->ShowOverlay(std::move(overlay));
    }
}

void Widget::HideOverlay() {
    if (tree_ != nullptr) {
        tree_->HideOverlay();
    }
}

void Widget::ClearSubtreeInteractionState() {
    // Focus AND hover are per-widget flags; hiding/disabling a parent must
    // not leave descendants stuck in either state (they would repaint with
    // a stale highlight if the subtree is shown again).
    focused_ = false;
    hovered_ = false;
    for (const auto& child : children_) {
        child->ClearSubtreeInteractionState();
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
    tree_ = parent ? parent->tree_ : nullptr;
    SetTreeRecursive(tree_);
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
    // The retained canvas keeps last frame's pixels: both the vacated area
    // in the old parent and the adopted area in the new one must repaint,
    // and paint_dirty_ must reach the tree or no WM_PAINT is scheduled.
    if (previous) {
        previous->Invalidate();
    }
    if (parent) {
        parent->Invalidate();
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
        // Called while the widget is still under construction (e.g. inside
        // a make_shared<T> ctor): shared ownership does not exist yet, so
        // registration is deferred until the widget joins a WidgetTree —
        // SetTreeRecursive() flushes pending_children_.
        pending_children_.push_back(std::move(child));
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
    if (tree_ != nullptr && !tree_->LayoutInProgress() &&
        (bounds.x != bounds_.x || bounds.y != bounds_.y ||
         bounds.width != bounds_.width || bounds.height != bounds_.height)) {
        // Moved/resized outside a layout pass: both old and new areas need
        // repainting or stale pixels remain.
        tree_->AddDamageRect(bounds_.Union(bounds));
    }
    bounds_ = bounds;
}

void Widget::Invalidate() {
    paint_dirty_ = true;
    for (auto ancestor = GetParent(); ancestor; ancestor = ancestor->GetParent()) {
        ancestor->paint_dirty_ = true;
    }
    // Report this widget's bounds as the repaint region. Skipped during
    // layout — a layout request already forces a full repaint.
    if (tree_ != nullptr && !tree_->LayoutInProgress()) {
        tree_->AddDamageRect(bounds_);
    }
}

void Widget::Invalidate(const Layout::Rect& region) {
    paint_dirty_ = true;
    for (auto ancestor = GetParent(); ancestor; ancestor = ancestor->GetParent()) {
        ancestor->paint_dirty_ = true;
    }
    // Same damage reporting as Invalidate() but clipped to `region` —
    // keeps a moving element (playhead column) from repainting the whole
    // widget bounds.
    if (tree_ != nullptr && !tree_->LayoutInProgress()) {
        tree_->AddDamageRect(bounds_.Intersect(region));
    }
}

void Widget::RequestLayout() {
    layout_dirty_ = true;
    for (auto ancestor = GetParent(); ancestor; ancestor = ancestor->GetParent()) {
        ancestor->layout_dirty_ = true;
    }
}

void Widget::RenderFocusRing(
    Rendering::Renderer& renderer,
    const Theme::Tokens& tokens,
    float radius
) const {
    if (!IsFocused()) {
        return;
    }
    constexpr float kRingOffset = 2.0f;
    const auto bounds = GetBounds();
    renderer.StrokeRect(
        Layout::Rect{
            bounds.x - kRingOffset,
            bounds.y - kRingOffset,
            bounds.width + 2.0f * kRingOffset,
            bounds.height + 2.0f * kRingOffset
        },
        tokens.ring,
        2.0f,
        radius + kRingOffset
    );
}

void Widget::TickTree(double dt_seconds) {
    if (!visible_) {
        return;
    }
    Tick(dt_seconds);
    for (const auto& child : children_) {
        if (child) {
            child->TickTree(dt_seconds);
        }
    }
}

Layout::Rect Widget::CenteredTextRect(
    Rendering::Renderer& renderer,
    std::string_view text,
    const Rendering::TextStyle& style,
    float horizontal_padding
) const {
    const auto bounds = GetBounds();
    const float width = std::max(0.0f, bounds.width - 2.0f * horizontal_padding);
    const auto sample = text.empty() ? std::string_view("Ag") : text;
    const float text_height = renderer.MeasureText(sample, style).height;
    const float y = bounds.y + std::max(0.0f, (bounds.height - text_height) * 0.5f);
    return Layout::Rect{bounds.x + horizontal_padding, y, width, text_height};
}

} // namespace XER::UI::Widgets
