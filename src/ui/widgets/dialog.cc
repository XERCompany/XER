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
constexpr float kButtonHeight = 32.0f;
constexpr float kButtonGap = 8.0f;
constexpr float kGripSize = 16.0f;
// Minimum sliver of the panel that must stay inside the host bounds
// while dragging (views keeps a similar margin).
constexpr float kDragMargin = 48.0f;

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

void Dialog::SetPanelWidth(float width) {
    panel_width_ = std::max(240.0f, width);
    RequestLayout();
    Invalidate();
}

void Dialog::SetPanelHeight(float height) {
    panel_height_ = std::max(0.0f, height);
    RequestLayout();
    Invalidate();
}

void Dialog::SetOnClosed(std::function<void()> callback) {
    on_closed_ = std::move(callback);
}

void Dialog::SetButtons(std::vector<Button> buttons) {
    buttons_ = std::move(buttons);
    RequestLayout();
    Invalidate();
}

void Dialog::SetOnResult(std::function<void(Result)> callback) {
    on_result_ = std::move(callback);
}

void Dialog::Accept() {
    result_ = Result::Accepted;
    confirmed_ = true;
    Close();
}

void Dialog::Reject() {
    if (result_ == Result::None) {
        result_ = Result::Rejected;
    }
    Close();
}

void Dialog::SetModal(bool modal) {
    if (modal_ == modal) {
        return;
    }
    modal_ = modal;
    Invalidate();
}

void Dialog::SetCloseOnEscape(bool close) {
    close_on_escape_ = close;
}

void Dialog::SetCloseOnScrimClick(bool close) {
    close_on_scrim_click_ = close;
}

void Dialog::SetResizable(bool resizable) {
    if (resizable_ == resizable) {
        return;
    }
    resizable_ = resizable;
    Invalidate();
}

void Dialog::SetMovable(bool movable) {
    movable_ = movable;
}

void Dialog::SetInitialFocus(std::shared_ptr<Widget> widget) {
    initial_focus_ = std::move(widget);
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
    if (on_result_) {
        on_result_(result_);
    }
    if (on_closed_) {
        on_closed_();
    }
}

Layout::Rect Dialog::PanelRect(const Layout::Rect& bounds) const {
    const float content_h = content_ != nullptr
        ? content_->GetContentHeight()
        : 0.0f;
    const float auto_h = kHeaderHeight + content_h + kFooterHeight + kPadding;
    float h = panel_height_ > 0.0f ? panel_height_ : auto_h;
    h = std::min(h, bounds.height - 32.0f);
    const float w = std::min(panel_width_, bounds.width - 32.0f);
    // Drag offset, clamped so a sliver of the panel always stays inside
    // the host bounds (the title can't be dragged off-screen).
    const float cx = bounds.x + (bounds.width - w) * 0.5f;
    const float cy = bounds.y + (bounds.height - h) * 0.5f;
    const float x = std::clamp(cx + panel_offset_.x,
                               bounds.x - w + kDragMargin,
                               bounds.x + bounds.width - kDragMargin);
    const float y = std::clamp(cy + panel_offset_.y,
                               bounds.y - h + kDragMargin,
                               bounds.y + bounds.height - kDragMargin);
    return Layout::Rect{x, y, w, h};
}

Layout::Rect Dialog::GripRect(const Layout::Rect& panel) const {
    return Layout::Rect{panel.x + panel.width - kGripSize - 4.0f,
                        panel.y + panel.height - kGripSize - 4.0f,
                        kGripSize, kGripSize};
}

int Dialog::HitCustomButton(const Layout::Point& position) const {
    for (size_t i = 0; i < button_rects_.size(); ++i) {
        if (button_rects_[i].Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float Dialog::ButtonWidth(const std::string& label,
                          ButtonStyle style) const {
    const float floor_w = style == ButtonStyle::Primary ||
                                  style == ButtonStyle::Destructive
                              ? 88.0f
                              : 76.0f;
    return std::max(floor_w,
                    static_cast<float>(label.size()) * 7.5f + 28.0f);
}

void Dialog::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    // Frosted backdrop: blur only — no dark scrim, the blur itself
    // separates the modal from the content behind it. Modeless panels
    // skip the scrim entirely so the content behind stays readable.
    if (modal_) {
        renderer.DrawBackdropBlur(bounds, 0.7f);
    }

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

    // Footer buttons (bottom-right). Custom rows render in order; the
    // legacy path keeps its Cancel (secondary) + Confirm (accent) pair.
    // Labels are optically centered.
    auto draw_button = [&](const Layout::Rect& rect, const std::string& label,
                           const Theme::Color& fill, const Theme::Color& text,
                           bool stroke) {
        renderer.FillRect(rect, fill, tokens.radius_medium);
        if (stroke) {
            renderer.StrokeRect(rect, tokens.border, 1.0f, tokens.radius_medium);
        }
        const Rendering::TextStyle s{text, tokens.font_body, 500};
        const auto sz = renderer.MeasureText(
            label.empty() ? std::string_view("Ag") : std::string_view(label), s);
        const float lw = label.empty() ? 0.0f : sz.width;
        renderer.DrawText(
            label,
            Layout::Rect{rect.x + std::max(0.0f, (rect.width - lw) * 0.5f),
                         rect.y + std::max(0.0f, (rect.height - sz.height) * 0.5f),
                         lw + 4.0f, sz.height + 2.0f},
            s);
    };
    // views-style fill mapping for the custom button row.
    auto draw_styled = [&](const Layout::Rect& rect,
                           const Button& button) {
        Theme::Color fill = tokens.secondary;
        Theme::Color text = tokens.foreground;
        bool stroke = false;
        switch (button.style) {
            case ButtonStyle::Primary:
                fill = tokens.accent;
                text = tokens.accent_foreground;
                break;
            case ButtonStyle::Outline:
                fill = tokens.surface;
                fill.a *= 0.9f;
                stroke = true;
                break;
            case ButtonStyle::Ghost:
                fill = tokens.surface;
                fill.a = 0.0f;
                break;
            case ButtonStyle::Destructive:
                fill = tokens.danger;
                text = tokens.danger_foreground;
                break;
            case ButtonStyle::Secondary:
            default:
                break;
        }
        draw_button(rect, button.label, fill, text, stroke);
    };
    if (!buttons_.empty()) {
        for (size_t i = 0; i < buttons_.size() && i < button_rects_.size();
             ++i) {
            draw_styled(button_rects_[i], buttons_[i]);
        }
    } else {
        if (!cancel_label_.empty()) {
            Theme::Color cancel_fill = tokens.surface;
            cancel_fill.a *= 0.9f;
            draw_button(cancel_rect_, cancel_label_, cancel_fill,
                        tokens.foreground, true);
        }
        draw_button(confirm_rect_, confirm_label_, tokens.accent,
                    tokens.accent_foreground, false);
    }

    // Resize grip: three short diagonal notches in the corner
    // (QSizeGrip visual language).
    if (resizable_) {
        const auto grip = GripRect(panel);
        Theme::Color grip_color = tokens.muted;
        grip_color.a *= 0.7f;
        for (int i = 0; i < 3; ++i) {
            const float off = static_cast<float>(i) * 5.0f;
            renderer.DrawLine(
                {grip.x + kGripSize - off - 2.0f,
                 grip.y + kGripSize - 1.0f},
                {grip.x + kGripSize - 1.0f,
                 grip.y + kGripSize - off - 2.0f},
                grip_color, 1.4f);
        }
    }
}

void Dialog::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void Dialog::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);

    // views::DialogDelegate initial focus: apply once the dialog lives
    // in a tree so the focus request sticks.
    if (!initial_focus_applied_ && initial_focus_ != nullptr &&
        GetTree() != nullptr) {
        initial_focus_->SetFocused(true);
        initial_focus_applied_ = true;
    }

    const auto panel = PanelRect(bounds);

    close_rect_ = Layout::Rect{panel.x + panel.width - 36.0f, panel.y + 8.0f, 28.0f, 28.0f};

    const float btn_y = panel.y + panel.height - kFooterHeight + 10.0f;
    if (!buttons_.empty()) {
        // Right-aligned row in declaration order (first button lands
        // rightmost of its predecessors — views button-row layout).
        button_rects_.clear();
        float x = panel.x + panel.width - kPadding;
        for (size_t i = buttons_.size(); i-- > 0;) {
            const float w = ButtonWidth(buttons_[i].label,
                                        buttons_[i].style);
            x -= w;
            // Store indexed in declaration order even though we place
            // right-to-left.
            button_rects_.insert(
                button_rects_.begin(),
                Layout::Rect{x, btn_y, w, kButtonHeight});
            x -= kButtonGap;
        }
    } else {
        const float confirm_w = std::max(
            88.0f, static_cast<float>(confirm_label_.size()) * 7.5f + 28.0f);
        const float cancel_w = cancel_label_.empty()
            ? 0.0f
            : std::max(76.0f, static_cast<float>(cancel_label_.size()) * 7.5f + 28.0f);
        const float gap = cancel_w > 0.0f ? 8.0f : 0.0f;
        cancel_rect_ = Layout::Rect{
            panel.x + panel.width - kPadding - confirm_w - gap - cancel_w,
            btn_y, cancel_w, 32.0f
        };
        confirm_rect_ = Layout::Rect{
            panel.x + panel.width - kPadding - confirm_w,
            btn_y, confirm_w, 32.0f
        };
    }

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
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                if (resizable_ && GripRect(panel).Contains(event.position)) {
                    resizing_ = true;
                    resize_anchor_ = event.position;
                    resize_w_ = panel.width;
                    resize_h_ = panel.height;
                    return true;
                }
                // Header strip (minus the close affordance) drags the
                // panel when movable.
                const Layout::Rect header{
                    panel.x, panel.y,
                    panel.width - (close_rect_.width + 8.0f),
                    kHeaderHeight};
                if (movable_ && header.Contains(event.position) &&
                    !close_rect_.Contains(event.position)) {
                    dragging_ = true;
                    drag_anchor_ = {
                        event.position.x - panel.x,
                        event.position.y - panel.y};
                    return true;
                }
            }
            return false;
        case Event::Type::MouseMove:
            if (resizing_) {
                const auto bounds = GetBounds();
                panel_width_ = std::clamp(
                    resize_w_ + (event.position.x - resize_anchor_.x),
                    240.0f, bounds.width - 32.0f);
                panel_height_ = std::clamp(
                    resize_h_ + (event.position.y - resize_anchor_.y),
                    kHeaderHeight + kFooterHeight + kPadding + 24.0f,
                    bounds.height - 32.0f);
                RequestLayout();
                Invalidate();
                return true;
            }
            if (dragging_) {
                const auto bounds = GetBounds();
                panel_offset_ = {
                    event.position.x - drag_anchor_.x -
                        (bounds.x + (bounds.width - panel.width) * 0.5f),
                    event.position.y - drag_anchor_.y -
                        (bounds.y + (bounds.height - panel.height) * 0.5f)};
                RequestLayout();
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (event.mouse_button == MouseButton::Left &&
                (dragging_ || resizing_)) {
                dragging_ = false;
                resizing_ = false;
                return true;
            }
            return false;
        case Event::Type::Click: {
            if (!buttons_.empty()) {
                const int hit = HitCustomButton(event.position);
                if (hit >= 0) {
                    const Button& b = buttons_[static_cast<size_t>(hit)];
                    if (b.action) {
                        b.action();
                    }
                    // Accept/reject rows close; Result::None stays open
                    // (the "Apply" pattern).
                    if (b.result == Result::Accepted) {
                        Accept();
                    } else if (b.result == Result::Rejected) {
                        Reject();
                    }
                    return true;
                }
            } else {
                if (confirm_rect_.Contains(event.position)) {
                    Accept();
                    return true;
                }
                if (!cancel_label_.empty() &&
                    cancel_rect_.Contains(event.position)) {
                    Reject();
                    return true;
                }
            }
            if (close_rect_.Contains(event.position)) {
                Reject();
                return true;
            }
            if (panel.Contains(event.position)) {
                return true; // swallow clicks inside the panel
            }
            if (!modal_) {
                // Modeless: outside clicks belong to the widgets behind.
                return false;
            }
            if (close_on_scrim_click_) {
                Reject(); // scrim click dismisses
            }
            return true;
        }
        case Event::Type::KeyDown:
            if (event.key == KeyCode::Escape) {
                if (close_on_escape_) {
                    Reject();
                }
                return true;
            }
            if (event.key == KeyCode::Enter) {
                // Default-button behavior: a custom row activates its
                // first Accept entry; the stock pair confirms.
                if (!buttons_.empty()) {
                    for (const auto& b : buttons_) {
                        if (b.result == Result::Accepted) {
                            if (b.action) {
                                b.action();
                            }
                            Accept();
                            return true;
                        }
                    }
                    return true;
                }
                Accept();
                return true;
            }
            // Modal: swallow every other key — a position-based Contains()
            // check is meaningless for key events and would leak keys to
            // handlers behind the dialog. Modeless panels pass them on.
            return modal_;
        default:
            return false;
    }
}

Layout::Size Dialog::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Dialog::GetPreferredSize() const {
    return {panel_width_, kPanelWidth * 0.57f};
}

Layout::Size Dialog::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
