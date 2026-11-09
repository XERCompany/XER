#include "dialog.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kPanelWidth = 420.0f;
constexpr float kHeaderHeight = 48.0f;
constexpr float kFooterHeight = 52.0f;
constexpr float kPadding = 20.0f;

} // namespace

std::shared_ptr<Dialog> Dialog::Create(
    std::string title,
    std::shared_ptr<Widget> content,
    std::string confirm_label,
    std::string cancel_label) {
    return std::shared_ptr<Dialog>(new Dialog(
        std::move(title), std::move(content), std::move(confirm_label),
        std::move(cancel_label)));
}

Dialog::Dialog(std::string title, std::shared_ptr<Widget> content,
               std::string confirm_label, std::string cancel_label)
    : title_(std::move(title))
    , content_(std::move(content))
    , confirm_label_(std::move(confirm_label))
    , cancel_label_(std::move(cancel_label)) {
    SetFocusable(true);
    if (content_ != nullptr) {
        AddChild(content_);
    }
}

void Dialog::SetTitle(std::string title) {
    title_ = std::move(title);
    Invalidate();
}

void Dialog::SetContent(std::shared_ptr<Widget> content) {
    if (content_ != nullptr) {
        RemoveChild(content_);
    }
    content_ = std::move(content);
    if (content_ != nullptr) {
        AddChild(content_);
    }
    RequestLayout();
    Invalidate();
}

void Dialog::SetOnClosed(std::function<void()> callback) {
    on_closed_ = std::move(callback);
}

void Dialog::Close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    // The tree has a single modal overlay slot: ShowOverlay() may have
    // replaced this dialog with another widget while the caller-held
    // shared_ptr kept the dialog alive. Only hide the overlay when it is
    // still ours, otherwise we'd tear down somebody else's popup.
    const auto self = weak_from_this().lock();
    if (self != nullptr && GetTree() != nullptr &&
        GetTree()->GetOverlay() == self) {
        HideOverlay();
    }
    if (on_closed_) {
        on_closed_();
    }
}

Layout::Rect Dialog::PanelRect(const Layout::Rect& bounds) const {
    const float content_h = content_ != nullptr
        ? content_->GetContentHeight()
        : 0.0f;
    const float height = kHeaderHeight + content_h + kFooterHeight + kPadding;
    const float h = std::min(height, bounds.height - 32.0f);
    const float w = std::min(kPanelWidth, bounds.width - 32.0f);
    return Layout::Rect{
        bounds.x + (bounds.width - w) * 0.5f,
        bounds.y + (bounds.height - h) * 0.5f,
        w,
        h
    };
}

void Dialog::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    // Frosted backdrop: blur only — no dark scrim, the blur itself
    // separates the modal from the content behind it.
    renderer.DrawBackdropBlur(bounds, 0.7f);

    const auto panel = PanelRect(bounds);
    // Glass panel: surface at ~94% so the dimmed backdrop bleeds through.
    Theme::Color panel_bg = tokens.surface;
    panel_bg.a *= 0.94f;
    renderer.FillRect(panel, panel_bg, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);

    // Header: title (dialog scale, not the 30px page font) + close glyph,
    // separated from content by a hairline.
    const float header_cy = panel.y + kHeaderHeight * 0.5f;
    Rendering::TextStyle title_style{tokens.foreground, 16.0f, 600};
    renderer.DrawText(
        title_,
        Layout::Rect{panel.x + kPadding, header_cy - 10.0f,
                     panel.width - 2.0f * kPadding - 44.0f, 20.0f},
        title_style
    );
    const Rendering::TextStyle close_style{tokens.muted, tokens.font_body, 400};
    const auto close_measure = renderer.MeasureText("×", close_style);
    renderer.DrawText(
        "×",
        Layout::Rect{close_rect_.x + (close_rect_.width - close_measure.width) * 0.5f,
                     close_rect_.y + (close_rect_.height - close_style.size) * 0.5f,
                     close_measure.width + 4.0f, close_style.size + 4.0f},
        close_style
    );
    Theme::Color hairline = tokens.border;
    hairline.a *= 0.6f;
    renderer.DrawLine(
        Layout::Point{panel.x + 1.0f, panel.y + kHeaderHeight},
        Layout::Point{panel.x + panel.width - 1.0f, panel.y + kHeaderHeight},
        hairline, 1.0f);

    // Footer buttons (bottom-right): optional Cancel (secondary) + Confirm
    // (accent). Labels are optically centered.
    auto draw_button = [&](const Layout::Rect& rect, const std::string& label,
                           const Theme::Color& fill, const Theme::Color& text,
                           bool stroke) {
        renderer.FillRect(rect, fill, tokens.radius_medium);
        if (stroke) {
            renderer.StrokeRect(rect, tokens.border, 1.0f, tokens.radius_medium);
        }
        const Rendering::TextStyle s{text, tokens.font_body, 500};
        const auto sz = renderer.MeasureText(label, s);
        renderer.DrawText(
            label,
            Layout::Rect{rect.x + (rect.width - sz.width) * 0.5f,
                         rect.y + (rect.height - s.size) * 0.5f,
                         sz.width + 4.0f, s.size + 4.0f},
            s);
    };
    if (!cancel_label_.empty()) {
        Theme::Color cancel_fill = tokens.surface;
        cancel_fill.a *= 0.9f;
        draw_button(cancel_rect_, cancel_label_, cancel_fill,
                    tokens.foreground, true);
    }
    draw_button(confirm_rect_, confirm_label_, tokens.accent,
                tokens.accent_foreground, false);
}

void Dialog::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void Dialog::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);
    const auto panel = PanelRect(bounds);

    close_rect_ = Layout::Rect{panel.x + panel.width - 36.0f, panel.y + 8.0f, 28.0f, 28.0f};

    const float confirm_w = 88.0f;
    const float cancel_w = cancel_label_.empty() ? 0.0f : 76.0f;
    const float gap = cancel_w > 0.0f ? 8.0f : 0.0f;
    const float btn_y = panel.y + panel.height - kFooterHeight + 10.0f;
    cancel_rect_ = Layout::Rect{
        panel.x + panel.width - kPadding - confirm_w - gap - cancel_w,
        btn_y, cancel_w, 32.0f
    };
    confirm_rect_ = Layout::Rect{
        panel.x + panel.width - kPadding - confirm_w,
        btn_y, confirm_w, 32.0f
    };

    if (content_ != nullptr) {
        const float content_h = std::max(0.0f, panel.height - kHeaderHeight - kFooterHeight - kPadding);
        content_->Layout(Layout::Rect{
            panel.x + kPadding,
            panel.y + kHeaderHeight,
            std::max(0.0f, panel.width - 2.0f * kPadding),
            content_h
        }, CachedLayoutTokens());
    }
}

bool Dialog::HandleEvent(const Event& event) {
    if (closed_) {
        return false;
    }
    const auto panel = PanelRect(GetBounds());
    switch (event.type) {
        case Event::Type::Click:
            if (confirm_rect_.Contains(event.position)) {
                confirmed_ = true;
                Close();
                return true;
            }
            if (!cancel_label_.empty() &&
                cancel_rect_.Contains(event.position)) {
                Close();
                return true;
            }
            if (close_rect_.Contains(event.position)) {
                Close();
                return true;
            }
            if (panel.Contains(event.position)) {
                return true; // swallow clicks inside the panel
            }
            Close(); // scrim click dismisses
            return true;
        case Event::Type::KeyDown:
            if (event.key == KeyCode::Escape) {
                Close();
                return true;
            }
            if (event.key == KeyCode::Enter) {
                confirmed_ = true;
                Close();
                return true;
            }
            return panel.Contains(event.position);
        default:
            return false;
    }
}

Layout::Size Dialog::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Dialog::GetPreferredSize() const {
    return {kPanelWidth, 240.0f};
}

Layout::Size Dialog::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
