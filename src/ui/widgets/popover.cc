#include "popover.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace {

// Floating panel rendered in the overlay layer. Passive surface + title;
// any click outside the panel rect dismisses.
class PopoverPanel final : public Widget {
public:
    PopoverPanel(Layout::Rect anchor, Popover::Side side, Popover::Align align,
                 std::string title, std::shared_ptr<Widget> content)
        : anchor_(anchor)
        , side_(side)
        , align_(align)
        , title_(std::move(title))
        , content_(std::move(content)) {
        SetFocusable(false);
        SetClipsChildren(true);
        open_t_.Set(1.0f, 0.14f);
        // Real child so the tree renders + hit-tests the content.
        if (content_) AddChild(content_);
    }

    void SetCallbacks(std::function<void()> on_dismiss) {
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
        const Theme::Color surface = tokens.surface;
        const Theme::Color border = tokens.border;
        (void)alpha;

        renderer.FillRect(bounds, surface, tokens.radius_large);
        renderer.StrokeRect(bounds, border, 1.0f, tokens.radius_large);

        const float pad = 14.0f;
        float y = bounds.y + pad;
        if (!title_.empty()) {
            const Rendering::TextStyle style{tokens.foreground,
                                             tokens.font_body, 600};
            Layout::Rect tr{bounds.x + pad, y, bounds.width - pad * 2.0f,
                            tokens.font_body * 1.4f};
            renderer.DrawText(title_, tr, style);
            y += tr.height + 8.0f;
        }
        // Content is a real child — RenderSubtree draws it; Layout()
        // assigns its rect.
    }

    void Layout(const Layout::Rect& bounds) override {
        // `bounds` is the full client rect the tree assigns overlays; the
        // panel repositions itself relative to the anchor instead.
        const float w = std::clamp(PreferredW(), 160.0f, 360.0f);
        const float h = std::clamp(PreferredH(), 80.0f, 480.0f);
        Layout::Rect r;
        r.width = w;
        r.height = h;

        const float gap = 8.0f;
        const float margin = 8.0f;
        switch (side_) {
            case Popover::Side::Top:
                r.y = anchor_.y - h - gap;
                break;
            case Popover::Side::Left:
                r.x = anchor_.x - w - gap;
                r.y = anchor_.y;
                break;
            case Popover::Side::Right:
                r.x = anchor_.x + anchor_.width + gap;
                r.y = anchor_.y;
                break;
            case Popover::Side::Bottom:
            default:
                r.y = anchor_.y + anchor_.height + gap;
                break;
        }
        if (side_ == Popover::Side::Bottom || side_ == Popover::Side::Top) {
            switch (align_) {
                case Popover::Align::Center:
                    r.x = anchor_.x + anchor_.width * 0.5f - w * 0.5f;
                    break;
                case Popover::Align::End:
                    r.x = anchor_.x + anchor_.width - w;
                    break;
                case Popover::Align::Start:
                default:
                    r.x = anchor_.x;
                    break;
            }
        } else {
            // Vertical sides center on the anchor.
            r.y = anchor_.y + anchor_.height * 0.5f - h * 0.5f;
        }
        // Clamp inside the client area (bounds = full client rect).
        if (r.x < margin) r.x = margin;
        if (r.y < margin) r.y = margin;
        if (r.x + w > bounds.width - margin)
            r.x = bounds.width - margin - w;
        if (r.y + h > bounds.height - margin)
            r.y = bounds.height - margin - h;
        SetBounds(r);

        // Position the content child inside the panel.
        if (content_) {
            const float pad = 14.0f;
            float cy = r.y + pad;
            if (!title_.empty()) cy += 14.0f * 1.4f + 8.0f;
            Layout::Rect cr{r.x + pad, cy, r.width - pad * 2.0f,
                            std::max(0.0f, r.y + r.height - cy - pad)};
            content_->Layout(cr, CachedLayoutTokens());
        }
    }

    bool HandleEvent(const Event& event) override {
        if (event.type == Event::Type::Click) {
            // Any click reaching the overlay that isn't consumed by content
            // dismisses the popover.
            if (on_dismiss_) on_dismiss_();
            return true;
        }
        return false;
    }

    Layout::Size GetMinSize() const override { return {160.0f, 80.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {PreferredW(), PreferredH()};
    }
    Layout::Size GetMaxSize() const override { return {360.0f, 480.0f}; }

private:
    float PreferredW() const {
        float w = 220.0f;
        if (!title_.empty())
            w = std::max(w, static_cast<float>(title_.size()) * 7.5f + 28.0f);
        if (content_) w = std::max(w, content_->GetPreferredSize().width + 28.0f);
        return w;
    }

    float PreferredH() const {
        float h = 28.0f;
        if (!title_.empty()) h += 14.0f * 1.4f + 8.0f;
        if (content_) h += content_->GetPreferredSize().height + 28.0f;
        return h;
    }

    Layout::Rect anchor_{};
    Popover::Side side_;
    Popover::Align align_;
    std::string title_;
    std::shared_ptr<Widget> content_;
    Motion::Tween open_t_;
    std::function<void()> on_dismiss_;
};

std::shared_ptr<PopoverPanel> LockedPanel(const std::weak_ptr<Widget>& w) {
    return std::dynamic_pointer_cast<PopoverPanel>(w.lock());
}

} // namespace

Popover::Popover() {
    SetFocusable(false);
}

Popover::~Popover() {
    Close();
}

void Popover::SetTrigger(std::shared_ptr<Widget> trigger) {
    trigger_ = std::move(trigger);
}

void Popover::SetContent(std::shared_ptr<Widget> content) {
    content_ = std::move(content);
}

void Popover::SetTitle(std::string title) {
    title_ = std::move(title);
}

void Popover::SetSide(Side side) {
    side_ = side;
}

void Popover::SetAlign(Align align) {
    align_ = align;
}

void Popover::SetOpen(bool open) {
    SyncOpen(open);
}

bool Popover::IsOpen() const {
    const auto panel = panel_.lock();
    return panel != nullptr && GetTree() != nullptr &&
           GetTree()->GetOverlay() == panel;
}

void Popover::Open() {
    SyncOpen(true);
}

void Popover::Close() {
    SyncOpen(false);
}

void Popover::SetOnOpenChange(std::function<void(bool)> callback) {
    on_open_change_ = std::move(callback);
}

void Popover::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (trigger_) trigger_->Layout(bounds, CachedLayoutTokens());
}

Layout::Size Popover::GetMinSize() const {
    return trigger_ ? trigger_->GetMinSize() : Layout::Size{0.0f, 0.0f};
}

Layout::Size Popover::GetPreferredSize() const {
    return trigger_ ? trigger_->GetPreferredSize()
                    : Layout::Size{0.0f, 0.0f};
}

Layout::Size Popover::GetMaxSize() const {
    return trigger_ ? trigger_->GetMaxSize()
                    : Layout::Size{100000.0f, 100000.0f};
}

void Popover::SyncOpen(bool open) {
    // Derive truth from the tree: the overlay may have been cleared by an
    // item action or Escape without going through Close().
    const bool actually = IsOpen();
    if (open == actually) {
        open_ = actually;
        return;
    }
    if (!open) {
        const auto panel = panel_.lock();
        const bool ours = panel != nullptr && GetTree() != nullptr &&
                          GetTree()->GetOverlay() == panel;
        panel_.reset();
        if (ours) HideOverlay();
        open_ = false;
        if (on_open_change_) on_open_change_(false);
        return;
    }
    if (GetTree() == nullptr || GetBounds().width <= 0.0f) return;
    auto panel = std::make_shared<PopoverPanel>(
        GetBounds(), side_, align_, title_, content_);
    const auto weak_self = weak_from_this();
    panel->SetCallbacks([weak_self]() {
        if (const auto self =
                std::static_pointer_cast<Popover>(weak_self.lock())) {
            self->Close();
        }
    });
    panel_ = panel;
    ShowOverlay(panel);
    open_ = true;
    if (on_open_change_) on_open_change_(true);
}

} // namespace XER::UI::Widgets
