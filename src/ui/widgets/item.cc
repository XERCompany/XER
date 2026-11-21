#include "item.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

// Same gesture window the list panels used for drawn rows.
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

}  // namespace

Item::Item() {
    SetFocusable(false);
}

void Item::SetTitle(std::string text) {
    if (title_ == text) return;
    title_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Item::SetDescription(std::string text) {
    if (description_ == text) return;
    description_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Item::SetSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    Invalidate();
}

void Item::SetOnClick(std::function<void()> callback) {
    on_click_ = std::move(callback);
}

void Item::SetOnDoubleClick(std::function<void()> callback) {
    on_double_click_ = std::move(callback);
}

void Item::Render(Rendering::Renderer& renderer,
                  const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (selected_) {
        renderer.FillRect(bounds, tokens.hover, tokens.radius_medium);
    }

    const auto& children = GetChildren();
    // A lone child occupies the leading slot; with 2+ children the
    // last one is the trailing slot (see Layout).
    const bool has_leading = !children.empty();
    const bool has_trailing = children.size() >= 2;

    float text_x = bounds.x + tokens.spacing_medium;
    float text_w = bounds.width - 2.0f * tokens.spacing_medium;

    if (has_leading) {
        text_x += 32.0f;
        text_w -= 32.0f;
    }
    if (has_trailing) {
        const auto& last = children.back();
        text_w -= std::min(last->GetPreferredSize().width + tokens.spacing_small,
                           text_w * 0.5f);
    }

    float y = bounds.y + tokens.spacing_small;
    if (!title_.empty()) {
        const Rendering::TextStyle style{tokens.foreground, tokens.font_body, 500};
        renderer.DrawText(title_, {text_x, y, text_w, tokens.font_body * 1.4f}, style);
        y += tokens.font_body * 1.4f;
    }
    if (!description_.empty()) {
        const Rendering::TextStyle style{tokens.muted, tokens.font_small, 400};
        renderer.DrawText(description_,
                          {text_x, y, text_w, tokens.font_small * 1.4f}, style);
    }
}

void Item::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& children = GetChildren();
    if (children.empty()) return;

    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float vpad = tokens.spacing_small;

    // First child = leading slot (a lone child included); with 2+
    // children the last is the trailing slot and the rest fill the
    // middle.
    Layout::Rect leading{bounds.x + pad, bounds.y + vpad, 28.0f,
                         std::max(0.0f, bounds.height - 2.0f * vpad)};
    children.front()->Layout(leading, tokens);
    if (children.size() == 1) {
        return;
    }

    const auto& last = children.back();
    const float trail_w =
        std::min(last->GetPreferredSize().width, bounds.width * 0.4f);
    Layout::Rect trailing{bounds.x + bounds.width - pad - trail_w,
                          bounds.y + vpad, trail_w,
                          std::max(0.0f, bounds.height - 2.0f * vpad)};
    last->Layout(trailing, tokens);

    // Middle children (if any) fill the space between leading and trailing.
    if (children.size() > 2) {
        Layout::Rect middle{bounds.x + pad + 32.0f, bounds.y + vpad,
                            std::max(0.0f, bounds.width - 2.0f * pad - 32.0f -
                                              trail_w - tokens.spacing_small),
                            std::max(0.0f, bounds.height - 2.0f * vpad)};
        for (std::size_t i = 1; i + 1 < children.size(); ++i) {
            children[i]->Layout(middle, tokens);
        }
    }
}

bool Item::HandleEvent(const Event& event) {
    // Rows with no click hooks stay event-transparent: their clicks
    // bubble to the parent chain (list panels own those).
    if (event.type != Event::Type::Click ||
        event.mouse_button != MouseButton::Left ||
        (on_click_ == nullptr && on_double_click_ == nullptr)) {
        return false;
    }
    // A second click inside the window additionally fires the
    // double-click hook — single click selects, double click applies.
    const auto now = std::chrono::steady_clock::now();
    const bool double_click =
        now - last_click_time_ < kDoubleClickWindow;
    last_click_time_ = now;
    if (on_click_ != nullptr) {
        on_click_();
    }
    if (double_click && on_double_click_ != nullptr) {
        on_double_click_();
    }
    return true;
}

Layout::Size Item::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Item::GetPreferredSize() const {
    float h = 0.0f;
    if (!title_.empty()) h += 20.0f;
    if (!description_.empty()) h += 18.0f;
    for (const auto& child : GetChildren()) {
        h = std::max(h, child->GetPreferredSize().height + 16.0f);
    }
    return {0.0f, std::max(h, 24.0f)};
}

Layout::Size Item::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
