#include "hover_card.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

// The floating card itself, shown in the overlay layer. Passive except
// for the optional action button at the bottom.
class HoverCardBubble final : public Widget {
public:
    HoverCardBubble(Layout::Rect anchor, std::string title,
                    std::string description,
                    std::shared_ptr<Widget> content,
                    std::string action_label)
        : anchor_(anchor)
        , title_(std::move(title))
        , description_(std::move(description))
        , content_(std::move(content))
        , action_label_(std::move(action_label)) {
        SetFocusable(false);
        open_t_.Set(1.0f, 0.14f);
    }

    void SetCallbacks(std::function<void()> on_action,
                      std::function<void()> on_dismiss) {
        on_action_ = std::move(on_action);
        on_dismiss_ = std::move(on_dismiss);
    }

    void Tick(double dt) override {
        if (!IsEffectivelyVisible()) return;
        open_t_.Update(static_cast<float>(dt));
        if (open_t_.Active()) Invalidate();
    }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const auto bounds = GetBounds();
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

        const float alpha = open_t_.Value();
        const Theme::Color surface =
            WithAlpha(tokens.surface, alpha);
        const Theme::Color border = WithAlpha(tokens.border, alpha);

        renderer.FillRect(bounds, surface, tokens.radius_large);
        renderer.StrokeRect(bounds, border, 1.0f, tokens.radius_large);

        const float pad = 14.0f;
        float y = bounds.y + pad;

        if (!title_.empty()) {
            const Rendering::TextStyle style{WithAlpha(tokens.foreground, alpha),
                                             tokens.font_body, 600};
            Layout::Rect tr{bounds.x + pad, y, bounds.width - pad * 2.0f,
                            tokens.font_body * 1.4f};
            renderer.DrawText(title_, tr, style);
            y += tr.height + 4.0f;
        }
        if (!description_.empty()) {
            const Rendering::TextStyle style{WithAlpha(tokens.muted, alpha),
                                             tokens.font_small, 400};
            const float h =
                static_cast<float>(Utf8Length(description_)) * 0 +
                tokens.font_small * 2.6f;
            Layout::Rect dr{bounds.x + pad, y, bounds.width - pad * 2.0f, h};
            renderer.DrawText(description_, dr, style);
            y += h + 4.0f;
        }
        if (content_) {
            const float ch = std::max(0.0f, bounds.y + bounds.height - y -
                                             (action_rect_.height > 0 ? 44.0f : pad));
            Layout::Rect cr{bounds.x + pad, y, bounds.width - pad * 2.0f, ch};
            content_->Layout(cr, tokens);
            content_->Render(renderer, tokens);
            y += ch;
        }
        if (!action_label_.empty()) {
            const float bh = 26.0f;
            Layout::Rect br{bounds.x + pad, bounds.y + bounds.height - pad - bh,
                            bounds.width - pad * 2.0f, bh};
            action_rect_ = br;
            const Theme::Color bg = action_hover_
                                        ? WithAlpha(tokens.accent, alpha)
                                        : WithAlpha(tokens.muted, alpha);
            renderer.FillRect(br, bg, tokens.radius_medium);
            const Rendering::TextStyle style{
                WithAlpha(action_hover_ ? tokens.accent_foreground
                                        : tokens.foreground,
                         alpha),
                tokens.font_small, 500};
            const auto ts = renderer.MeasureText(action_label_, style);
            Layout::Rect tr{br.x + (br.width - ts.width) * 0.5f,
                            br.y + (br.height - ts.height) * 0.5f,
                            ts.width, ts.height};
            renderer.DrawText(action_label_, tr, style);
        }
        (void)y;
    }

    void Layout(const Layout::Rect& bounds) override {
        // `bounds` is the full client area the tree assigns overlays; the
        // bubble repositions itself relative to the anchor instead.
        (void)bounds;
        const float w = std::min(320.0f, std::max(200.0f, PreferredW()));
        const float h = std::min(360.0f, PreferredH());
        Layout::Rect r;
        r.width = w;
        r.height = h;
        r.x = anchor_.x + anchor_.width * 0.5f - w * 0.5f;
        // Below the anchor; flip above when there isn't room — the tree
        // gives us the full client area via our own bounds at layout time,
        // so use a fixed margin heuristic.
        r.y = anchor_.y + anchor_.height + 8.0f;
        if (r.y + h > 800.0f) r.y = anchor_.y - h - 8.0f;
        // Clamp horizontally inside the window.
        if (r.x < 8.0f) r.x = 8.0f;
        SetBounds(r);
    }

    bool HandleEvent(const Event& event) override {
        if (!action_label_.empty() && action_rect_.width > 0.0f) {
            const auto& p = event.position;
            const bool inside =
                p.x >= action_rect_.x && p.x < action_rect_.x + action_rect_.width &&
                p.y >= action_rect_.y && p.y < action_rect_.y + action_rect_.height;
            switch (event.type) {
                case Event::Type::MouseMove:
                    if (inside != action_hover_) {
                        action_hover_ = inside;
                        Invalidate();
                    }
                    return inside;
                case Event::Type::Click:
                    if (inside && event.mouse_button == MouseButton::Left) {
                        if (on_action_) on_action_();
                        if (on_dismiss_) on_dismiss_();
                        return true;
                    }
                    return false;
                default:
                    break;
            }
        }
        // Click outside the card dismisses (scrim-like behavior): any
        // click that reaches us but isn't on the action closes.
        if (event.type == Event::Type::Click) {
            if (on_dismiss_) on_dismiss_();
            return true;
        }
        return false;
    }

    Layout::Size GetMinSize() const override { return {200.0f, 80.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {PreferredW(), PreferredH()};
    }
    Layout::Size GetMaxSize() const override { return {400.0f, 400.0f}; }

private:
    static Theme::Color WithAlpha(Theme::Color c, float alpha) {
        c.a = c.a * alpha;
        return c;
    }

    float PreferredW() const {
        float w = 200.0f;
        if (!title_.empty())
            w = std::max(w, static_cast<float>(Utf8Length(title_)) * 7.5f + 28.0f);
        if (!description_.empty())
            w = std::max(w, static_cast<float>(Utf8Length(description_)) * 6.5f + 28.0f);
        return std::min(320.0f, w);
    }

    float PreferredH() const {
        // Approximate heights at font_body≈14 / font_small≈12.
        float h = 28.0f;
        if (!title_.empty()) h += 14.0f * 1.4f + 4.0f;
        if (!description_.empty()) h += 12.0f * 2.6f + 4.0f;
        if (content_) h += 80.0f;
        if (!action_label_.empty()) h += 44.0f;
        return h;
    }

    Layout::Rect anchor_{};
    std::string title_;
    std::string description_;
    std::shared_ptr<Widget> content_;
    std::string action_label_;
    Layout::Rect action_rect_{};
    bool action_hover_ = false;
    Motion::Tween open_t_;
    std::function<void()> on_action_;
    std::function<void()> on_dismiss_;
};

std::shared_ptr<HoverCardBubble> LockedCard(const std::weak_ptr<Widget>& w) {
    return std::dynamic_pointer_cast<HoverCardBubble>(w.lock());
}

} // namespace

HoverCard::HoverCard() {
    SetFocusable(false);
}

HoverCard::~HoverCard() {
    CloseCard();
}

void HoverCard::SetTrigger(std::shared_ptr<Widget> trigger) {
    trigger_ = std::move(trigger);
}

void HoverCard::SetTitle(std::string title) {
    title_ = std::move(title);
}

void HoverCard::SetDescription(std::string description) {
    description_ = std::move(description);
}

void HoverCard::SetContent(std::shared_ptr<Widget> content) {
    content_ = std::move(content);
}

void HoverCard::SetOpenDelay(float seconds) {
    open_delay_ = std::max(0.0f, seconds);
}

void HoverCard::SetOnAction(std::function<void()> callback) {
    on_action_ = std::move(callback);
}

void HoverCard::SetActionLabel(std::string label) {
    action_label_ = std::move(label);
}

void HoverCard::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (trigger_) trigger_->Layout(bounds, CachedLayoutTokens());
}

Layout::Size HoverCard::GetMinSize() const {
    return trigger_ ? trigger_->GetMinSize() : Layout::Size{0.0f, 0.0f};
}

Layout::Size HoverCard::GetPreferredSize() const {
    return trigger_ ? trigger_->GetPreferredSize()
                    : Layout::Size{0.0f, 0.0f};
}

Layout::Size HoverCard::GetMaxSize() const {
    return trigger_ ? trigger_->GetMaxSize()
                    : Layout::Size{100000.0f, 100000.0f};
}

void HoverCard::Tick(double dt_seconds) {
    const bool hovered = trigger_ && trigger_->IsHovered();
    if (hovered) {
        hover_time_ += static_cast<float>(dt_seconds);
        close_grace_ = 0.0f;
        if (!IsOpen() && hover_time_ >= open_delay_) {
            OpenCard();
        }
    } else {
        hover_time_ = 0.0f;
        if (IsOpen()) {
            // Grace period: keep the card briefly so the pointer can move
            // onto it diagonally without it vanishing.
            close_grace_ += static_cast<float>(dt_seconds);
            if (close_grace_ > 0.35f) CloseCard();
        }
    }
}

bool HoverCard::IsOpen() const {
    const auto card = card_.lock();
    return card != nullptr && GetTree() != nullptr &&
           GetTree()->GetOverlay() == card;
}

void HoverCard::OpenCard() {
    if (GetTree() == nullptr || GetBounds().width <= 0.0f) return;
    if (IsOpen()) return;
    auto bubble = std::make_shared<HoverCardBubble>(
        GetBounds(), title_, description_, content_, action_label_);
    const auto weak_self = weak_from_this();
    bubble->SetCallbacks(
        [weak_self]() {
            if (const auto self =
                    std::static_pointer_cast<HoverCard>(weak_self.lock())) {
                if (self->on_action_) self->on_action_();
            }
        },
        [weak_self]() {
            if (const auto self =
                    std::static_pointer_cast<HoverCard>(weak_self.lock())) {
                self->CloseCard();
            }
        });
    card_ = bubble;
    ShowOverlay(bubble);
}

void HoverCard::CloseCard() {
    const auto card = card_.lock();
    const bool ours = card != nullptr && GetTree() != nullptr &&
                      GetTree()->GetOverlay() == card;
    card_.reset();
    if (ours) HideOverlay();
}

} // namespace XER::UI::Widgets
