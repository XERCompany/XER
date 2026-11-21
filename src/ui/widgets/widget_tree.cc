#include "widget_tree.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../debug/debug_internal.h"
#include "core/ui_events.h"
#include "rendering/renderer.h"
#include "style/widget_style_host.h"
#include "theme/tokens.h"
#include "widgets/tooltip.h"

namespace XER::UI::Widgets {
namespace {

// Reads a computed-style float (opacity etc.); returns fallback when
// the property is absent or unparsable.
float StyleFloat(const Widget& w, std::string_view prop,
                 float fallback) {
    const std::string* v = w.StyleValue(prop);
    if (v == nullptr) {
        return fallback;
    }
    try {
        return std::stof(*v);
    } catch (...) {
        return fallback;
    }
}

// Parses `translate(x,y)` / `scale(x[,y])` fragments from the CSS
// `transform` value into `out` — enough for element offsets/zooms
// carried into the render worker. Returns true when a transform was
// specified.
bool ParseStyleTransform(const Widget& w, RenderTransform* out) {
    const std::string* v = w.StyleValue("transform");
    if (v == nullptr || v->empty() || *v == "none") {
        return false;
    }
    bool any = false;
    auto args = [&](std::string_view fn, std::vector<float>* parsed) {
        const auto open = v->find(std::string(fn) + "(");
        if (open == std::string::npos) {
            return;
        }
        const auto close = v->find(')', open);
        const std::string body =
            v->substr(open + fn.size() + 1, close - open - fn.size() - 1);
        for (const auto& part : std::views::split(body, ',')) {
            try {
                parsed->push_back(
                    std::stof(std::string(part.begin(), part.end())));
            } catch (...) {
            }
        }
    };
    std::vector<float> t, s;
    args("translate", &t);
    args("scale", &s);
    if (t.size() >= 2) {
        out->tx = t[0];
        out->ty = t[1];
        any = true;
    }
    if (!s.empty()) {
        out->a = s[0];
        out->d = s.size() > 1 ? s[1] : s[0];
        any = true;
    }
    return any;
}

// parent ∘ child — applies `child` first, then `parent`: the child's
// transform is expressed in the parent's coordinate space, so nested
// transformed elements accumulate instead of clobbering each other.
RenderTransform ComposeTransform(const RenderTransform& parent,
                                 const RenderTransform& child) {
    return {parent.a * child.a + parent.c * child.b,
            parent.b * child.a + parent.d * child.b,
            parent.a * child.c + parent.c * child.d,
            parent.b * child.c + parent.d * child.d,
            parent.a * child.tx + parent.c * child.ty + parent.tx,
            parent.b * child.tx + parent.d * child.ty + parent.ty};
}

// RAII unwinder for one element's renderer state — on ANY exit path
// (including exceptions from widget->Render or a child's subtree) it
// pops clip → transform → layer in reverse push order, restoring the
// parent's transform matrix so the remote worker's stacks stay
// balanced.
struct ElementScope {
    Rendering::Renderer& renderer;
    std::vector<RenderTransform>* stack;
    bool clipped = false;
    bool transformed = false;
    bool layered = false;
    ElementScope(Rendering::Renderer& r,
                 std::vector<RenderTransform>* s)
        : renderer(r), stack(s) {}
    ElementScope(const ElementScope&) = delete;
    ElementScope& operator=(const ElementScope&) = delete;
    ~ElementScope() {
        if (clipped) {
            renderer.PopClip();
        }
        if (transformed) {
            stack->pop_back();
            if (stack->empty()) {
                renderer.ResetTransform();
            } else {
                const RenderTransform& m = stack->back();
                renderer.SetTransform(m.a, m.b, m.c, m.d, m.tx, m.ty);
            }
        }
        if (layered) {
            renderer.PopLayer();
        }
    }
};

}  // namespace

void WidgetTree::SetRoot(std::shared_ptr<Widget> root) {
    ClearFocus(root_);
    HideTooltip();
    // UpdateHover(nullptr) also clears hovered_=false on the old widget;
    // a bare weak_ptr reset would leave it stuck in the hover state.
    UpdateHover(nullptr);
    root_ = std::move(root);
    RequestFullDamage();
    if (root_ != nullptr) {
        root_->SetTreeRecursive(this);
    }
}

void WidgetTree::ShowOverlay(std::shared_ptr<Widget> overlay) {
    HideTooltip();
    ClearFocus(overlay_);
    // Modal layer: drop stale hover state on the main tree so no widget
    // keeps a hover highlight while it is unreachable.
    UpdateHover(nullptr);
    overlay_ = std::move(overlay);
    RequestFullDamage();
    if (overlay_ != nullptr) {
        overlay_->SetTreeRecursive(this);
        overlay_->SetBounds(last_bounds_);
        overlay_->Layout(last_bounds_, last_tokens_);
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
    UpdateHover(nullptr);
    overlay_.reset();
    RequestFullDamage();
}

std::shared_ptr<Widget> WidgetTree::GetRoot() const {
    return root_;
}

void WidgetTree::Layout(const Layout::Rect& bounds) {
    Layout(bounds, last_tokens_);
}

void WidgetTree::SetStyleHost(
    std::shared_ptr<CSS::WidgetStyleHost> host) {
    style_host_ = std::move(host);
    // Styles can change sizes — relayout + repaint.
    if (root_) root_->RequestLayout();
    RequestFullDamage();
}

void WidgetTree::SetStyleSheet(std::string_view css_text) {
    if (!style_host_)
        style_host_ = std::make_shared<CSS::WidgetStyleHost>();
    style_host_->SetStyleSheet(css_text);
    if (root_) root_->RequestLayout();
    RequestFullDamage();
}

void WidgetTree::AddStyleSheet(std::string_view css_text) {
    if (!style_host_)
        style_host_ = std::make_shared<CSS::WidgetStyleHost>();
    style_host_->AddStyleSheet(css_text);
    if (root_) root_->RequestLayout();
    RequestFullDamage();
}

void WidgetTree::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    if (style_host_) style_host_->Update(root_);
    last_bounds_ = bounds;
    last_tokens_ = tokens;
    layout_in_progress_ = true;
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
    layout_in_progress_ = false;
}

void WidgetTree::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens,
                        const Layout::Rect* damage) const {
    if (style_host_) style_host_->Update(root_);
    if (root_ == nullptr || !root_->IsVisible()) {
        return;
    }

    const Layout::Rect bounds = root_->GetBounds();
    // Clip = damage (or full bounds). A 1px margin keeps anti-aliased edges
    // at the damage boundary from being shaved by the aliased clip.
    Layout::Rect clip =
        (damage != nullptr && damage->width > 0.0f && damage->height > 0.0f)
            ? *damage
            : bounds;
    clip.x -= 1.0f;
    clip.y -= 1.0f;
    clip.width += 2.0f;
    clip.height += 2.0f;

    renderer.SetFrameDamage(clip);
    renderer.BeginFrame(bounds);
    // A freshly recreated canvas has no retained pixels: ignoring the
    // damage clip for this one frame prevents clear-color voids.
    const bool fresh_targets = renderer.TakeTargetsRecreated();
    if (fresh_targets) {
        clip = bounds;
    }
    renderer.PushClip(clip);
    // Transform stack for the whole frame: each element's CSS
    // transform composes onto its parent's matrix; RenderSubtree's
    // scope guards pop exactly what they push.
    std::vector<RenderTransform> transform_stack;
    try {
        RenderSubtree(root_, renderer, tokens,
                      fresh_targets ? nullptr : &clip, &transform_stack);
        if (overlay_ != nullptr) {
            RenderSubtree(overlay_, renderer, tokens,
                          fresh_targets ? nullptr : &clip,
                          &transform_stack);
        }
        if (passive_overlay_ != nullptr) {
            RenderSubtree(passive_overlay_, renderer, tokens,
                          fresh_targets ? nullptr : &clip,
                          &transform_stack);
        }
    } catch (...) {
        // Balance the frame clip before the throw escapes — an
        // unbalanced PushClip would clip every later frame.
        renderer.PopClip();
        throw;
    }
    renderer.PopClip();
    renderer.EndFrame();
}

bool WidgetTree::HandleEvent(const Event& event) {
    if (root_ == nullptr) {
        return false;
    }
    Event filtered_event = event;
    if (event_bus_ != nullptr && !event_bus_->Dispatch(filtered_event)) {
        // A filter swallowed the event; it counts as handled.
        return true;
    }
    const Event& evt = filtered_event;
    // Presses, wheel and keys dismiss a visible tooltip immediately.
    if (evt.type == Event::Type::MouseDown ||
        evt.type == Event::Type::Click ||
        evt.type == Event::Type::MouseWheel ||
        evt.type == Event::Type::KeyDown) {
        HideTooltip();
    }
    // While an overlay is shown it owns all pointer input (modal semantics);
    // the overlay widget itself decides whether to dismiss on outside clicks.
    const auto& input_root = overlay_ != nullptr ? overlay_ : root_;
    if (suppress_duplicate_click_ && evt.type != Event::Type::Click) {
        suppress_duplicate_click_ = false;
        synthesized_click_target_.reset();
    }

    if (evt.type == Event::Type::KeyDown && evt.key == KeyCode::Tab) {
        return MoveFocus(evt.is_shift);
    }

    switch (evt.type) {
    case Event::Type::MouseWheel: {
        auto target = FindHitTarget(input_root, evt.position, true);
        // Wheel events bubble up the parent chain so scroll containers can
        // react even when a leaf widget (slider, button) is under the cursor.
        while (target != nullptr) {
            if (target->HandleEvent(evt)) {
                return true;
            }
            target = target->GetParent();
        }
        return false;
    }
    case Event::Type::MouseMove: {
        last_pointer_position_ = evt.position;
        // While the left button is held, mouse messages stay routed to the
        // widget that received MouseDown (mouse-capture semantics), which is
        // what makes slider dragging work outside the widget bounds.
        const auto captured = left_mouse_down_target_.lock();
        const auto target = captured != nullptr
            ? captured
            : FindHitTarget(input_root, evt.position, true);
        // Deliberately NOT bubbled (unlike Click/MouseWheel): hover state
        // already reaches the deepest leaf via UpdateHover/SetHovered, so
        // no parent fallback is needed; and move events are high
        // frequency — walking ancestors on every unhandled move would run
        // each parent's hover bookkeeping per pixel moved. Drag capture
        // also relies on single-target routing.
        if (captured == nullptr) {
            UpdateHover(target);
        }
        if (target == nullptr) {
            return false;
        }
        return target->HandleEvent(evt);
    }
    case Event::Type::Click: {
        last_pointer_position_ = evt.position;
        const auto target = FindHitTarget(input_root, evt.position, true);
        if (suppress_duplicate_click_) {
            const auto synthesized_target = synthesized_click_target_.lock();
            const bool duplicate = evt.mouse_button == MouseButton::Left &&
                target == synthesized_target &&
                evt.position.x == synthesized_click_position_.x &&
                evt.position.y == synthesized_click_position_.y;
            suppress_duplicate_click_ = false;
            synthesized_click_target_.reset();
            if (duplicate) {
                return true;
            }
        }
        if (target == nullptr) {
            return false;
        }
        return DispatchClick(target, evt);
    }
    case Event::Type::MouseDown: {
        last_pointer_position_ = evt.position;
        const auto target = FindHitTarget(input_root, evt.position, true);
        if (evt.mouse_button == MouseButton::Left) {
            left_mouse_down_target_ = target;
        }
        UpdateHover(target);
        if (target == nullptr) {
            if (evt.mouse_button == MouseButton::Right && context_menu_handler_) {
                return context_menu_handler_(evt);
            }
            return false;
        }
        if (target->IsFocusable()) {
            SetFocusedWidget(target);
        }
        const bool handled = target->HandleEvent(evt);
        // Right-clicks that no widget consumed bubble to the tree-level
        // context-menu handler (e.g. AudioPlayerWidget's menu).
        if (!handled && evt.mouse_button == MouseButton::Right &&
            context_menu_handler_) {
            return context_menu_handler_(evt);
        }
        return handled;
    }
    case Event::Type::MouseUp: {
        last_pointer_position_ = evt.position;
        const auto hit = FindHitTarget(input_root, evt.position, true);
        const auto pressed_target = left_mouse_down_target_.lock();
        if (evt.mouse_button == MouseButton::Left) {
            left_mouse_down_target_.reset();
            // Capture held hover frozen during the drag; refresh it now so
            // the widget under the cursor gets its highlight back.
            UpdateHover(hit);
        }
        const auto target = evt.mouse_button == MouseButton::Left &&
                pressed_target != nullptr
            ? pressed_target
            : hit;
        const bool released = target != nullptr && target->HandleEvent(evt);
        if (evt.mouse_button != MouseButton::Left ||
            hit == nullptr || hit != pressed_target) {
            return released;
        }

        Event click = event;
        click.type = Event::Type::Click;
        suppress_duplicate_click_ = true;
        synthesized_click_target_ = target;
        synthesized_click_position_ = evt.position;
        // The synthesized click bubbles exactly like a real Click so
        // behavior never depends on which path delivered it.
        return DispatchClick(target, click) || released;
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
    if (focused != focusable.end() && (*focused)->HandleEvent(evt)) {
        return true;
    }
    // Give the overlay root a last shot at keys the focused widget ignored
    // (e.g. Escape closing a Dialog while a child input has focus).
    if (overlay_ != nullptr && overlay_->HandleEvent(evt)) {
        return true;
    }
    // Global shortcuts: only reached when nothing focused consumed the key,
    // so typing in a TextInput never triggers transport shortcuts. A modal
    // overlay owns all input — shortcuts must not fire behind it.
    if (key_handler_ != nullptr && overlay_ == nullptr &&
        (evt.type == Event::Type::KeyDown || evt.type == Event::Type::KeyUp ||
         evt.type == Event::Type::TextInput)) {
        return key_handler_(evt);
    }
    return false;
}

bool WidgetTree::DispatchClick(
    const std::shared_ptr<Widget>& target,
    const Event& event) {
    // Click bubbles up the GetParent() chain — the same rule MouseWheel
    // already uses — so a leaf that doesn't handle clicks (a list Item,
    // a decorative label, a drawn-row gap) lets its container react
    // instead of the event dying at the leaf. The chain is a strict
    // parent walk, so it always terminates at the root; a widget can
    // never see the same click twice because only the first `true`
    // stops the walk.
    bool consumed = false;
    for (std::shared_ptr<Widget> widget = target; widget != nullptr;
         widget = widget->GetParent()) {
        if (widget->HandleEvent(event)) {
            consumed = true;
            break;
        }
    }
    if (!consumed) {
        // Dead click — the pre-bubbling rule: a directly hit focusable
        // widget still takes focus; anything else leaves focus alone.
        if (target->IsFocusable()) {
            SetFocusedWidget(target);
        }
        return false;
    }
    // The click was handled: focus the first focusable widget on the
    // target→root path. Usually that's the target itself (same as
    // before); for inert children (list Items) focus lands on the
    // interactive container, so arrow-key navigation keeps working
    // after a row click. Note MouseDown already focused a focusable
    // target, so this only ever moves focus upward, never sideways.
    for (std::shared_ptr<Widget> widget = target; widget != nullptr;
         widget = widget->GetParent()) {
        if (widget->IsFocusable()) {
            SetFocusedWidget(widget);
            break;
        }
    }
    return true;
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
    const Theme::Tokens& tokens,
    const Layout::Rect* cull,
    std::vector<RenderTransform>* transform_stack
) const {
    if (widget == nullptr || !widget->IsVisible()) {
        return;
    }

    // Skip subtrees that cannot intersect the damage clip. The pad covers
    // draw overflow beyond widget bounds (focus rings, popover shadows).
    if (cull != nullptr) {
        constexpr float kCullPad = 48.0f;
        const Layout::Rect& b = widget->GetBounds();
        if (b.x + b.width < cull->x - kCullPad ||
            b.x > cull->x + cull->width + kCullPad ||
            b.y + b.height < cull->y - kCullPad ||
            b.y > cull->y + cull->height + kCullPad) {
            return;
        }
    }

    // Per-element compositing: the computed style may ask for an
    // offscreen layer (opacity / filter) or an affine transform —
    // remote render workers receive them as stream ops, so every
    // element's effects are evaluated process-side.
    const float opacity = StyleFloat(*widget, "opacity", 1.0f);
    const float blur = [&] {
        const std::string* f = widget->StyleValue("filter");
        if (f == nullptr) {
            return 0.0f;
        }
        const auto open = f->find("blur(");
        if (open == std::string::npos) {
            return 0.0f;
        }
        try {
            return std::stof(f->substr(open + 5));
        } catch (...) {
            return 0.0f;
        }
    }();
    const bool layered = opacity < 1.0f || blur > 0.0f;
    RenderTransform local{};
    const bool transformed = ParseStyleTransform(*widget, &local);

    // Every renderer state pushed below is unwound by this scope on
    // ALL exit paths — an exception must not leave the remote worker's
    // clip/transform/layer stacks unbalanced.
    ElementScope scope{renderer, transform_stack};
    if (transformed) {
        // Compose onto the parent's matrix so nested transforms
        // accumulate; siblings restore the parent transform on unwind.
        const RenderTransform parent =
            transform_stack->empty() ? RenderTransform{}
                                     : transform_stack->back();
        transform_stack->push_back(ComposeTransform(parent, local));
        scope.transformed = true;
        const RenderTransform& m = transform_stack->back();
        renderer.SetTransform(m.a, m.b, m.c, m.d, m.tx, m.ty);
    }
    if (layered) {
        renderer.PushLayer(opacity, 0u, blur);
        scope.layered = true;
    }

    try {
        widget->Render(renderer, tokens);
    } catch (const std::exception& error) {
        // Tag the failure with the widget so render errors are
        // self-diagnosing (type, bounds, debug text).
        const auto& b = widget->GetBounds();
        throw std::runtime_error(std::format(
            "{} render failed at ({:.1f},{:.1f} {:.1f}x{:.1f}) "
            "text='{}': {}",
            Debug::internal::QualifiedName(*widget), b.x, b.y,
            b.width, b.height, widget->GetDebugText(), error.what()));
    }
    const bool clips = widget->clips_children_;
    if (clips) {
        renderer.PushClip(widget->bounds_);
        scope.clipped = true;
    }
    for (const auto& child : widget->GetChildren()) {
        RenderSubtree(child, renderer, tokens, cull, transform_stack);
    }
    // ~ElementScope pops clip → transform → layer in reverse order.
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

void WidgetTree::AddDamageRect(Layout::Rect rect) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    // Inflate for focus rings, popover shadows (~16px) and AA fringe.
    constexpr float kPad = 24.0f;
    rect.x -= kPad;
    rect.y -= kPad;
    rect.width += 2.0f * kPad;
    rect.height += 2.0f * kPad;
    pending_damage_ =
        pending_damage_valid_ ? pending_damage_.Union(rect) : rect;
    pending_damage_valid_ = true;
}

WidgetTree::DamageRect WidgetTree::ConsumeDamageRect() {
    DamageRect out{pending_damage_, pending_damage_valid_};
    pending_damage_ = Layout::Rect{};
    pending_damage_valid_ = false;
    return out;
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

void WidgetTree::CancelPointerCapture() {
    const auto target = left_mouse_down_target_.lock();
    left_mouse_down_target_.reset();
    suppress_duplicate_click_ = false;
    synthesized_click_target_.reset();
    if (target != nullptr) {
        // Deliver a synthetic MouseUp so widgets end their drag/press state
        // (slider dragging_, scrollbar dragging_, button held_) instead of
        // waiting for an up event that will never arrive.
        Event up;
        up.type = Event::Type::MouseUp;
        up.mouse_button = MouseButton::Left;
        up.position = last_pointer_position_;
        target->HandleEvent(up);
    }
    const auto& input_root = overlay_ != nullptr ? overlay_ : root_;
    UpdateHover(FindHitTarget(input_root, last_pointer_position_, true));
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
    // Tooltip::Layout(bounds) hides the base two-arg overload; the base
    // version only delegates to it anyway and Tooltip has no children.
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
        overlay_->TickTree(dt_seconds);
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
