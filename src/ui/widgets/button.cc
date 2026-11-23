#include "button.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace {

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

constexpr float kPi = 3.14159265358979323846f;

// Icon layout metrics (DIP).
constexpr float kIconSize = 16.0f;
constexpr float kIconGap = 6.0f;

// Shortcut hint + menu chevron metrics (DIP).
constexpr float kShortcutGap = 10.0f;
constexpr float kShortcutCharWidth = 6.5f;
constexpr float kChevronWidth = 10.0f;
constexpr float kChevronGap = 6.0f;

// Per-size metrics (DIP).
constexpr float kHeightSmall = 28.0f;
constexpr float kHeightDefault = 36.0f;
constexpr float kHeightLarge = 44.0f;
constexpr float kMinWidthSmall = 56.0f;
constexpr float kMinWidthDefault = 64.0f;
constexpr float kMinWidthLarge = 80.0f;

// Process-wide icon resolver installed by the host app (see
// Button::SetIconResolver). Empty => every button stays text-only.
Button::IconResolver g_icon_resolver;

// Small downward chevron (menu affordance) centered in `box`.
void DrawChevron(Rendering::Renderer& renderer, Layout::Rect box,
                 Theme::Color color) {
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    constexpr float kHalf = 3.0f;
    renderer.DrawLine({cx - kHalf, cy - 1.5f}, {cx, cy + 1.5f}, color, 1.4f);
    renderer.DrawLine({cx, cy + 1.5f}, {cx + kHalf, cy - 1.5f}, color, 1.4f);
}

} // namespace

Button::Button(std::string text, ButtonStyle style)
    : text_(std::move(text))
    , style_(style) {
    SetFocusable(true);
    press_.Snap(0.0f);
    hover_t_.Set(0.0f);
}

Button::~Button() {
    // If our menu is still showing, release the overlay so it cannot
    // outlive the button that owns its item actions.
    const auto menu = menu_.lock();
    if (menu != nullptr && GetTree() != nullptr &&
        GetTree()->GetOverlay() == menu) {
        HideOverlay();
    }
}

void Button::SetText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

const std::string& Button::GetText() const {
    return text_;
}

void Button::SetOnClick(std::function<void()> callback) {
    on_click_ = std::move(callback);
}

void Button::SetStyle(ButtonStyle style) {
    style_ = style;
    Invalidate();
}

void Button::SetSize(ButtonSize size) {
    if (size_ == size) {
        return;
    }
    size_ = size;
    Invalidate();
    RequestLayout();
}

void Button::SetIconResolver(IconResolver resolver) {
    g_icon_resolver = std::move(resolver);
}

void Button::SetIcon(std::string icon_name) {
    if (icon_ == icon_name) {
        return;
    }
    icon_ = std::move(icon_name);
    Invalidate();
    RequestLayout();
}

void Button::SetIconPosition(IconPosition position) {
    if (icon_position_ == position) {
        return;
    }
    icon_position_ = position;
    Invalidate();
}

void Button::SetLoading(bool loading) {
    if (loading_ == loading) {
        return;
    }
    loading_ = loading;
    Invalidate();
}

void Button::SetCheckable(bool checkable) {
    if (checkable_ == checkable) {
        return;
    }
    checkable_ = checkable;
    if (!checkable_) {
        checked_ = false;
    }
    Invalidate();
}

void Button::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    Invalidate();
}

void Button::SetOnCheckedChanged(std::function<void(bool)> callback) {
    on_checked_changed_ = std::move(callback);
}

void Button::SetDefault(bool is_default) {
    if (is_default_ == is_default) {
        return;
    }
    is_default_ = is_default;
    Invalidate();
}

void Button::SetAutoDefault(bool auto_default) {
    auto_default_ = auto_default;
}

void Button::SetShortcut(std::string shortcut) {
    if (shortcut_ == shortcut) {
        return;
    }
    shortcut_ = std::move(shortcut);
    Invalidate();
    RequestLayout();
}

void Button::SetMenu(std::vector<ContextMenu::Item> items) {
    menu_items_ = std::move(items);
    // Refresh a live menu so new items appear without reopen.
    const auto menu = menu_.lock();
    if (menu != nullptr && GetTree() != nullptr &&
        GetTree()->GetOverlay() == menu) {
        HideOverlay();
        menu_.reset();
        OpenMenu();
    }
    Invalidate();
    RequestLayout();
}

void Button::ClearMenu() {
    const auto menu = menu_.lock();
    if (menu != nullptr && GetTree() != nullptr &&
        GetTree()->GetOverlay() == menu) {
        HideOverlay();
    }
    menu_.reset();
    menu_items_.clear();
    Invalidate();
    RequestLayout();
}

bool Button::IsMenuOpen() const {
    const auto menu = menu_.lock();
    return menu != nullptr && GetTree() != nullptr &&
           GetTree()->GetOverlay() == menu;
}

void Button::OpenMenu() {
    if (menu_items_.empty() || GetTree() == nullptr) {
        return;
    }
    const auto b = GetBounds();
    if (b.width <= 0.0f) {
        return;
    }
    auto menu = ContextMenu::Create(menu_items_);
    // Anchor just below the button's bottom-left corner (same
    // convention as DropdownMenu).
    menu->SetAnchor({b.x, b.y + b.height + 4.0f});
    menu_ = menu;
    ShowOverlay(menu);
    Invalidate();
}

float Button::HeightForSize() const {
    switch (size_) {
        case ButtonSize::Small: return kHeightSmall;
        case ButtonSize::Large: return kHeightLarge;
        case ButtonSize::Default:
        default: return kHeightDefault;
    }
}

void Button::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    const float want_hover = IsHovered() && IsEnabled() ? 1.0f : 0.0f;
    if (hover_t_.to != want_hover) {
        hover_t_.Set(want_hover, 0.12f);
    }
    hover_t_.Update(static_cast<float>(dt_seconds));
    press_.Update(static_cast<float>(dt_seconds));
    if (loading_) {
        spinner_angle_ = std::fmod(spinner_angle_ +
                                      static_cast<float>(dt_seconds) * 1.4f,
                                  1.0f);
    }
    if (hover_t_.Active() || !press_.Settled() || loading_) {
        Invalidate();
    }
}

void Button::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float press = press_.pos;
    const float hov = hover_t_.Value();
    // A latched checkable button reads held: fold checked_ into the
    // effective press so every style gets its pressed treatment.
    const float on = checked_ ? 0.85f : 0.0f;
    const float depth = std::max(press, on);

    // Press depth: the whole surface sinks ~1.5px while held.
    Layout::Rect surface = bounds;
    surface.y += press * 1.5f;
    surface.height = std::max(1.0f, surface.height - press * 1.5f);
    const float radius = tokens.radius_medium;

    Theme::Color fill_color{};
    Theme::Color stroke_color{};
    Theme::Color text_color{};
    bool has_fill = true;
    bool has_stroke = false;

    const auto lerp = [](const Theme::Color& a, const Theme::Color& b, float t) {
        return Motion::LerpColor(a, b, t);
    };

    if (!IsEnabled()) {
        // Disabled reads as a muted control, not a filled block: recessed
        // secondary fill + hairline border + dimmed text. Ghost/Outline stay
        // unfilled so they don't turn into gray pills.
        const bool flat = style_ == ButtonStyle::Ghost ||
                          style_ == ButtonStyle::Outline;
        has_fill = !flat;
        fill_color = tokens.secondary;
        stroke_color = tokens.border;
        text_color = tokens.disabled;
        has_stroke = !flat || style_ == ButtonStyle::Outline;
    } else {
        // Loading keeps the resting style colors; the spinner carries state.
        switch (style_) {
            case ButtonStyle::Primary:
                // Tailwind/Headless UI primary: accent fill. Hover deepens
                // toward foreground — darkens indigo-600 on light, lightens
                // indigo-500 on dark (matches Tailwind hover direction).
                fill_color = lerp(tokens.accent, tokens.foreground, hov * 0.14f);
                fill_color.a *= 1.0f - hov * 0.05f;
                text_color = tokens.accent_foreground;
                break;
            case ButtonStyle::Secondary:
                // shadcn secondary: quiet zinc fill, no border — hover lifts
                // one step toward `pressed`, press settles all the way.
                fill_color = lerp(tokens.secondary, tokens.pressed,
                                  std::max(hov, depth));
                text_color = tokens.foreground;
                break;
            case ButtonStyle::Outline:
                // shadcn outline: transparent + hairline border; hover
                // introduces a quiet fill, press deepens it toward
                // `pressed` (icon-button chrome stays flat otherwise).
                has_fill = hov > 0.01f || depth > 0.01f;
                fill_color = lerp(
                    lerp(tokens.surface, tokens.hover, hov),
                    tokens.pressed, depth * 0.7f);
                stroke_color = checked_ ? tokens.focus : tokens.border;
                text_color = checked_ && hov < 0.01f
                    ? lerp(tokens.foreground, tokens.focus, 0.35f)
                    : tokens.foreground;
                has_stroke = true;
                break;
            case ButtonStyle::Ghost:
                // NLE icon chrome: flat at rest with a muted glyph,
                // subtle hover wash, `pressed` fill while held.
                has_fill = hov > 0.01f || depth > 0.01f;
                fill_color = lerp(
                    lerp(tokens.surface, tokens.hover, hov),
                    tokens.pressed, depth * 0.7f);
                text_color = lerp(tokens.muted, tokens.foreground,
                                  std::max(hov, depth));
                if (checked_) {
                    // Latched ghost: accent-tinted text marks the state.
                    text_color = lerp(text_color, tokens.focus, 0.4f);
                }
                break;
            case ButtonStyle::Destructive:
                fill_color = lerp(tokens.danger, tokens.danger_foreground, hov * 0.12f);
                fill_color.a *= 1.0f - hov * 0.05f;
                text_color = tokens.danger_foreground;
                break;
        }
    }

    if (has_fill) {
        renderer.FillRect(surface, fill_color, radius);
    }
    if (has_stroke) {
        renderer.StrokeRect(surface, stroke_color, 1.0f, radius);
    }
    // Press shading: a faint foreground wash sells the depth without a shadow
    // primitive (works on both light and dark themes).
    if (press > 0.01f && IsEnabled()) {
        Theme::Color shade = tokens.foreground;
        shade.a = 0.07f * press;
        renderer.FillRect(surface, shade, radius);
    }
    // Checked latch affordance on filled styles: a hairline accent ring
    // inside the surface (Outline already strokes accent when checked).
    if (checked_ && IsEnabled() && style_ != ButtonStyle::Outline) {
        Theme::Color latch = tokens.focus;
        latch.a *= 0.9f;
        renderer.StrokeRect(
            Layout::Rect{surface.x + 1.0f, surface.y + 1.0f,
                         std::max(0.0f, surface.width - 2.0f),
                         std::max(0.0f, surface.height - 2.0f)},
            latch, 1.0f, std::max(0.0f, radius - 1.0f));
    }
    // Dialog default affordance (views::MdTextButton): accent ring just
    // inside the focus-ring offset so the default action reads at a
    // glance even unfocused.
    if (is_default_ && IsEnabled()) {
        renderer.StrokeRect(
            Layout::Rect{surface.x - 2.0f, surface.y - 2.0f,
                         surface.width + 4.0f, surface.height + 4.0f},
            tokens.focus, 1.0f, radius + 2.0f);
    }

    const Rendering::TextStyle style{text_color, tokens.font_body, 500};
    const bool has_chevron = HasMenu();
    if (loading_) {
        // Loading shifts the label right so it clears the spinner; the preferred
        // size is unchanged, so the button never resizes.
        const float text_pad = tokens.spacing_medium + 16.0f;
        Layout::Rect text_rect = CenteredTextRect(renderer, text_, style, text_pad);
        text_rect.x += 8.0f;
        text_rect.width = std::max(0.0f, text_rect.width - 16.0f);
        renderer.DrawText(text_, text_rect, style);
    } else {
        const auto sample = text_.empty() ? std::string_view("Ag") : std::string_view(text_);
        const auto measured = renderer.MeasureText(sample, style);
        const float text_w = text_.empty() ? 0.0f : measured.width;
        const float text_h = measured.height;
        const float min_pad = size_ == ButtonSize::Small ? 6.0f : 8.0f;
        // Icon sits left of the label; icon-only buttons center it.
        // The (icon + gap + text + shortcut + chevron) block centers as
        // a unit so the pieces read as one control.
        const bool has_icon = !icon_.empty() &&
                              static_cast<bool>(g_icon_resolver);
        const float icon_block =
            has_icon ? kIconSize + (text_w > 0.0f ? kIconGap : 0.0f)
                     : 0.0f;
        const float shortcut_w = shortcut_.empty()
            ? 0.0f
            : kShortcutGap +
                  static_cast<float>(Utf8Length(shortcut_)) *
                      kShortcutCharWidth;
        const float chevron_w = has_chevron ? kChevronGap + kChevronWidth
                                            : 0.0f;
        const float avail_w = std::max(0.0f, surface.width - 2.0f * min_pad);
        const float content_w = icon_block + text_w + shortcut_w + chevron_w;
        float x = (content_w < avail_w)
            ? (surface.x + min_pad + (avail_w - content_w) * 0.5f)
            : (surface.x + min_pad);
        const float icon_y = surface.y +
            std::max(0.0f, (surface.height - kIconSize) * 0.5f);
        const float y = surface.y + std::max(0.0f, (surface.height - text_h) * 0.5f);
        auto draw_icon = [&] {
            const Layout::Rect icon_box{x, icon_y, kIconSize, kIconSize};
            g_icon_resolver(renderer, icon_.c_str(),
                            static_cast<int>(kIconSize), text_color,
                            icon_box);
            x += icon_block;
        };
        if (has_icon && icon_position_ == IconPosition::Leading) {
            draw_icon();
        }
        const float max_w = std::max(0.0f, surface.Right() - min_pad - x);
        const float draw_w = (content_w < avail_w)
            ? std::min(max_w, text_w + 2.0f)
            : avail_w;
        // Squeezed below its measured width the label would word-wrap
        // mid-word (DrawText wraps, it doesn't clip) — ellipsize to
        // stay on one line instead.
        const std::string shown = text_w > draw_w
            ? EllipsizeText(renderer, text_, style, draw_w)
            : text_;
        renderer.DrawText(shown, Layout::Rect{x, y, draw_w, text_h},
                          style);
        x += draw_w;
        if (has_icon && icon_position_ == IconPosition::Trailing) {
            draw_icon();
        }
        if (shortcut_w > 0.0f) {
            const Rendering::TextStyle hint_style{
                tokens.muted, tokens.font_small, 400};
            renderer.DrawText(
                shortcut_,
                Layout::Rect{x + kShortcutGap, y, shortcut_w, text_h},
                hint_style);
            x += shortcut_w;
        }
        if (has_chevron) {
            DrawChevron(
                renderer,
                Layout::Rect{x + kChevronGap,
                             surface.y +
                                 (surface.height - 8.0f) * 0.5f,
                             kChevronWidth, 8.0f},
                text_color);
        }
    }

    if (loading_) {
        // 8-spoke arc spinner, same visual language as the Spinner widget.
        const float cx = bounds.x + 16.0f;
        const float cy = bounds.y + bounds.height * 0.5f;
        const float r_out = 5.0f;
        const float r_in = r_out * 0.45f;
        const float angle_offset = spinner_angle_ * 2.0f * kPi;
        for (int spoke = 0; spoke < 8; ++spoke) {
            const float angle = angle_offset +
                2.0f * kPi * static_cast<float>(spoke) / 8.0f;
            Theme::Color spoke_color = tokens.muted;
            spoke_color.a = 0.2f + 0.8f * static_cast<float>(spoke) / 7.0f;
            renderer.DrawLine(
                {cx + std::cos(angle) * r_in, cy + std::sin(angle) * r_in},
                {cx + std::cos(angle) * r_out, cy + std::sin(angle) * r_out},
                spoke_color, 1.6f);
        }
    }
    RenderFocusRing(renderer, tokens, radius);
}

void Button::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Button::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        // A widget disabled mid-press would otherwise stay "held" forever.
        if (held_) {
            held_ = false;
            press_.SetTarget(0.0f);
        }
        return false;
    }

    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                held_ = true;
                press_.SetTarget(1.0f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (held_ && event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_.SetTarget(0.0f);
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            // Releasing outside the button still ends the press (mouse capture
            // delivers the up to us); nothing else to do here.
            return false;
        case Event::Type::Click:
            if (event.mouse_button == MouseButton::Left) {
                held_ = false;
                press_.SetTarget(0.0f);
                if (loading_) {
                    return true; // busy: consume but don't fire
                }
                if (HasMenu()) {
                    // MenuButton semantics: activation opens the menu
                    // anchored below instead of firing on_click_.
                    OpenMenu();
                    return true;
                }
                Activate();
                return true;
            }
            return false;
        case Event::Type::KeyDown:
            if (IsFocused() &&
                (event.key == KeyCode::Enter || event.key == KeyCode::Space)) {
                if (loading_) {
                    return true;
                }
                if (HasMenu()) {
                    OpenMenu();
                    return true;
                }
                Activate();
                return true;
            }
            return false;
        default:
            return false;
    }
}

Layout::Size Button::GetMinSize() const {
    // Icon-only affordance: a bare icon button only needs the glyph plus
    // its padding — the text-button minimums would force icon tool
    // strips twice as wide as they can honestly render.
    if (text_.empty() && !icon_.empty()) {
        const float pad = size_ == ButtonSize::Small ? 6.0f : 8.0f;
        return {kIconSize + 2.0f * pad +
                    (HasMenu() ? kChevronGap + kChevronWidth : 0.0f),
                HeightForSize()};
    }
    const float w = size_ == ButtonSize::Small ? kMinWidthSmall
                    : size_ == ButtonSize::Large ? kMinWidthLarge
                                                 : kMinWidthDefault;
    return {w, HeightForSize()};
}

Layout::Size Button::GetPreferredSize() const {
    const float shortcut_w = shortcut_.empty()
        ? 0.0f
        : kShortcutGap +
              static_cast<float>(Utf8Length(shortcut_)) *
                  kShortcutCharWidth;
    const float chevron_w = HasMenu() ? kChevronGap + kChevronWidth : 0.0f;
    // Icon-only: glyph + padding (+ menu chevron) — no text metrics.
    if (text_.empty() && !icon_.empty()) {
        const float pad = size_ == ButtonSize::Small ? 6.0f : 8.0f;
        return {kIconSize + 2.0f * pad + chevron_w, HeightForSize()};
    }
    return {
        std::max(GetMinSize().width,
                 static_cast<float>(Utf8Length(text_)) * 7.7f +
                     32.0f + (loading_ ? 16.0f : 0.0f) +
                     (icon_.empty() ? 0.0f : kIconSize + kIconGap) +
                     shortcut_w + chevron_w),
        HeightForSize()
    };
}

Layout::Size Button::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void Button::Activate() {
    if (checkable_) {
        checked_ = !checked_;
        Invalidate();
        if (on_checked_changed_) {
            on_checked_changed_(checked_);
        }
    }
    if (auto_default_) {
        is_default_ = true;
        Invalidate();
    }
    if (on_click_) {
        on_click_();
    }
}

} // namespace XER::UI::Widgets
