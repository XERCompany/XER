#include "message_scroller.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kGap = 8.0f;
constexpr float kPad = 8.0f;

} // namespace

MessageScroller::MessageScroller() {
    SetFocusable(false);
    SetClipsChildren(true);
}

void MessageScroller::AddMessage(std::shared_ptr<Widget> message) {
    if (!message) return;
    messages_.push_back(std::move(message));
    stick_bottom_ = true;
    RequestLayout();
    Invalidate();
}

void MessageScroller::Clear() {
    messages_.clear();
    scroll_y_ = 0.0f;
    stick_bottom_ = true;
    RequestLayout();
    Invalidate();
}

void MessageScroller::ScrollToBottom() {
    stick_bottom_ = true;
    scroll_y_ = ContentHeight() - GetBounds().height;
    Invalidate();
}

float MessageScroller::ContentHeight() const {
    float h = kPad * 2.0f;
    for (const auto& m : messages_) {
        h += m->GetPreferredSize().height + kGap;
    }
    return h;
}

void MessageScroller::RelayoutChildren(const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float view_h = bounds.height;
    const float content_h = ContentHeight();
    const float max_scroll = std::max(0.0f, content_h - view_h);
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll);
    if (stick_bottom_) {
        scroll_y_ = max_scroll;
    }

    float y = bounds.y + kPad - scroll_y_;
    for (const auto& m : messages_) {
        const float h = m->GetPreferredSize().height;
        Layout::Rect cell;
        cell.x = bounds.x + kPad;
        cell.y = y;
        cell.width = std::max(0.0f, bounds.width - kPad * 2.0f);
        cell.height = h;
        m->Layout(cell, tokens);
        y += h + kGap;
    }
}

void MessageScroller::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    RelayoutChildren(CachedLayoutTokens());
}

void MessageScroller::Render(Rendering::Renderer& renderer,
                             const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    renderer.FillRect(bounds, tokens.background, tokens.radius_medium);
    renderer.PushClip(bounds);
    for (const auto& m : messages_) m->Render(renderer, tokens);
    renderer.PopClip();
}

bool MessageScroller::HandleEvent(const Event& event) {
    // Wheel scrolls the list; other events pass to children via tree.
    if (event.type == Event::Type::MouseWheel) {
        const float content_h = ContentHeight();
        const float view_h = GetBounds().height;
        const float max_scroll = std::max(0.0f, content_h - view_h);
        const float delta = static_cast<float>(event.wheel_delta) * 0.5f;
        const float before = scroll_y_;
        scroll_y_ = std::clamp(scroll_y_ - delta, 0.0f, max_scroll);
        stick_bottom_ = scroll_y_ >= max_scroll - 1.0f;
        if (scroll_y_ != before) {
            RequestLayout();
            Invalidate();
            return true;
        }
    }
    return false;
}

Layout::Size MessageScroller::GetMinSize() const {
    return {200.0f, 120.0f};
}

Layout::Size MessageScroller::GetPreferredSize() const {
    return {360.0f, 480.0f};
}

Layout::Size MessageScroller::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

float MessageScroller::GetContentHeight() const {
    return ContentHeight();
}

} // namespace XER::UI::Widgets
