#include "pie_menu.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kScreenPad = 8.0f;
constexpr float kIconSize = 16.0f;
constexpr float kIconGap = 3.0f;

// Process-wide icon lookup (same DI pattern as Button::SetIconResolver).
PieMenu::IconResolver g_icon_resolver;

// Normalizes an angle into [0, 2*pi).
float NormAngle(float a) {
    while (a < 0.0f) a += 2.0f * kPi;
    while (a >= 2.0f * kPi) a -= 2.0f * kPi;
    return a;
}

// Shortest signed angular distance from `from` to `to` in (-pi, pi].
float AngleDelta(float from, float to) {
    float d = NormAngle(to) - NormAngle(from);
    if (d > kPi) d -= 2.0f * kPi;
    if (d < -kPi) d += 2.0f * kPi;
    return d;
}

} // namespace

std::shared_ptr<PieMenu> PieMenu::Create(std::vector<Item> items) {
    return std::shared_ptr<PieMenu>(new PieMenu(std::move(items)));
}

PieMenu::PieMenu(std::vector<Item> items)
    : items_(std::move(items)) {
    SetFocusable(true);
    // Popover entrance: quick ease-out fade + scale-in from the hub.
    open_t_.Set(1.0f, 0.15f);
}

void PieMenu::SetIconResolver(IconResolver resolver) {
    g_icon_resolver = std::move(resolver);
}

void PieMenu::SetItems(std::vector<Item> items) {
    items_ = std::move(items);
    // Live re-flow: the ring recomputes angles from items_.size() every
    // frame; only the highlight/selection indices need re-clamping.
    if (highlighted_ >= static_cast<int>(items_.size())) {
        highlighted_ = -1;
    }
    if (selected_ >= static_cast<int>(items_.size())) {
        selected_ = -1;
    }
    Invalidate();
}

void PieMenu::SetCenter(Layout::Point position) {
    center_ = position;
    Invalidate();
}

void PieMenu::SetRadius(float radius) {
    radius_ = std::max(32.0f, radius);
    Invalidate();
}

void PieMenu::SetDeadZone(float radius) {
    dead_zone_ = std::clamp(radius, 4.0f, radius_ * 0.5f);
    Invalidate();
}

void PieMenu::SetOnSelect(std::function<void(int)> callback) {
    on_select_ = std::move(callback);
}

void PieMenu::SetOnHighlight(std::function<void(int)> callback) {
    on_highlight_ = std::move(callback);
}

void PieMenu::SetOnCancel(std::function<void()> callback) {
    on_cancel_ = std::move(callback);
}

void PieMenu::Close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    // Same single-overlay-slot guard as ContextMenu::Close: only hide
    // the tree's overlay when it is still this menu.
    const auto self = weak_from_this().lock();
    if (self != nullptr && GetTree() != nullptr &&
        GetTree()->GetOverlay() == self) {
        HideOverlay();
    }
}

Layout::Point PieMenu::Hub() const {
    const auto bounds = GetBounds();
    const float r = radius_ + kScreenPad;
    // Unset center (0,0 top-left) defaults to the middle of the surface.
    Layout::Point c = center_;
    if (c.x == 0.0f && c.y == 0.0f) {
        c = {bounds.x + bounds.width * 0.5f,
             bounds.y + bounds.height * 0.5f};
    }
    c.x = std::clamp(c.x, bounds.x + r, bounds.x + bounds.width - r);
    c.y = std::clamp(c.y, bounds.y + r, bounds.y + bounds.height - r);
    return c;
}

float PieMenu::RingOuter() const {
    const float p = ticked_ ? open_t_.Value() : 1.0f;
    const float e = Motion::EaseOutBack(p);
    return std::max(1.0f, radius_ * e);
}

float PieMenu::ItemAngle(int index) const {
    const int n = static_cast<int>(items_.size());
    if (n <= 0) {
        return 0.0f;
    }
    // Item 0 at the top, items clockwise in screen space.
    return -kPi * 0.5f +
           static_cast<float>(index) * (2.0f * kPi / static_cast<float>(n));
}

int PieMenu::HitWedge(const Layout::Point& position) const {
    if (items_.empty()) {
        return -1;
    }
    const auto hub = Hub();
    const float dx = position.x - hub.x;
    const float dy = position.y - hub.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < dead_zone_) {
        return -2; // dead zone: nothing highlighted, release cancels
    }
    const int n = static_cast<int>(items_.size());
    const float slice = 2.0f * kPi / static_cast<float>(n);
    // Angle of the pointer; wedge i spans theta_i +/- slice/2.
    const float a = NormAngle(std::atan2(dy, dx));
    const float base = NormAngle(-kPi * 0.5f - slice * 0.5f);
    int index = static_cast<int>(std::floor((a - base) / slice));
    index = ((index % n) + n) % n;
    // Aiming past the ring still selects by direction (marking-menu
    // convention); aiming inside the ring does the same.
    return index;
}

void PieMenu::SetHighlighted(int index) {
    if (index < 0 ||
        (index < static_cast<int>(items_.size()) &&
         !items_[static_cast<std::size_t>(index)].enabled) ||
        index >= static_cast<int>(items_.size())) {
        // -1 outside the ring, -2 dead zone — both read as "no item".
        index = -1;
    }
    if (index == highlighted_) {
        return;
    }
    highlighted_ = index;
    if (index >= 0) {
        indicator_target_ = ItemAngle(index);
        if (!indicator_has_target_) {
            indicator_angle_ = indicator_target_;
            indicator_has_target_ = true;
        }
    }
    Invalidate();
    if (on_highlight_) {
        on_highlight_(index);
    }
}

void PieMenu::Finish(int index) {
    if (index >= 0 && index < static_cast<int>(items_.size()) &&
        items_[static_cast<std::size_t>(index)].enabled) {
        selected_ = index;
        const int i = index;
        auto action = items_[static_cast<std::size_t>(i)].action;
        Close();
        if (on_select_) {
            on_select_(i);
        }
        if (action) {
            action();
        }
        return;
    }
    Cancel();
}

void PieMenu::Cancel() {
    if (on_cancel_) {
        on_cancel_();
    }
    Close();
}

void PieMenu::MoveHighlightByDirection(float angle) {
    // Directional aiming (marking-menu keyboard model): the arrow key
    // selects the enabled wedge whose center direction is closest to
    // the key's direction — ArrowUp aims at the top wedge, not merely
    // "next item".
    int best = -1;
    float best_dist = 1e9f;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (!items_[static_cast<std::size_t>(i)].enabled) {
            continue;
        }
        const float d = std::fabs(AngleDelta(angle, ItemAngle(i)));
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    SetHighlighted(best);
}

void PieMenu::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    if (!ticked_ && rendered_) {
        // Already painted at full opacity on the first frame (same
        // guard as ContextMenu): snap so the first tick doesn't dip.
        open_t_.Set(1.0f, 0.0f);
    }
    ticked_ = true;
    open_t_.Update(static_cast<float>(dt_seconds));
    // Hub indicator turns toward the highlighted item the short way.
    if (highlighted_ >= 0) {
        const float diff = AngleDelta(indicator_angle_, indicator_target_);
        const float step = Motion::ReducedMotion()
            ? diff
            : diff * std::min(1.0f, static_cast<float>(dt_seconds) * 14.0f);
        indicator_angle_ = NormAngle(indicator_angle_ + step);
    }
    if (open_t_.Active() || highlighted_ >= 0) {
        Invalidate();
    }
}

void PieMenu::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const float p = ticked_ ? open_t_.Value() : 1.0f;
    rendered_ = true;
    const auto FA = [p](Theme::Color col) {
        col.a *= p;
        return col;
    };

    const auto hub = Hub();
    const float r = RingOuter();
    const float dead = dead_zone_;
    const int n = static_cast<int>(items_.size());

    // Soft elevation shadow behind the ring.
    renderer.DrawBackdropBlur(
        Layout::Rect{hub.x - r - 8.0f, hub.y - r - 8.0f,
                     2.0f * (r + 8.0f), 2.0f * (r + 8.0f)},
        0.7f);

    // Outer disc — opaque surface so labels stay readable over anything.
    Theme::Color disc = tokens.surface;
    disc.a = 1.0f;
    renderer.FillRect({hub.x - r, hub.y - r, 2.0f * r, 2.0f * r},
                      FA(disc), r);

    // Highlighted wedge: a fan of spokes fills the sector (the renderer
    // has no pie primitive). Danger items tint danger, normal hover.
    if (highlighted_ >= 0 && highlighted_ < n) {
        const float slice = 2.0f * kPi / static_cast<float>(n);
        const float a0 = ItemAngle(highlighted_) - slice * 0.5f + 0.015f;
        const float a1 = ItemAngle(highlighted_) + slice * 0.5f - 0.015f;
        const auto& item = items_[static_cast<std::size_t>(highlighted_)];
        Theme::Color wedge = item.danger
            ? Theme::Color{tokens.danger.r, tokens.danger.g,
                           tokens.danger.b, 0.20f}
            : tokens.hover;
        wedge = FA(wedge);
        // Angular step keeps ~1.4px arc spacing at the outer edge.
        const float step = std::max(0.012f, 1.4f / r);
        for (float a = a0; a <= a1; a += step) {
            const float cs = std::cos(a), sn = std::sin(a);
            renderer.DrawLine(
                {hub.x + cs * (dead + 1.0f), hub.y + sn * (dead + 1.0f)},
                {hub.x + cs * (r - 2.0f), hub.y + sn * (r - 2.0f)},
                wedge, 2.2f);
        }
    }

    // Wedge separators.
    if (n > 1) {
        const float slice = 2.0f * kPi / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const float a = ItemAngle(i) + slice * 0.5f;
            const float cs = std::cos(a), sn = std::sin(a);
            renderer.DrawLine(
                {hub.x + cs * (dead + 1.0f), hub.y + sn * (dead + 1.0f)},
                {hub.x + cs * (r - 2.0f), hub.y + sn * (r - 2.0f)},
                FA(tokens.border), 1.0f);
        }
    }

    // Ring border + recessed dead-zone hub.
    renderer.StrokeRect({hub.x - r, hub.y - r, 2.0f * r, 2.0f * r},
                        FA(tokens.border), 1.0f, r);
    renderer.FillRect({hub.x - dead, hub.y - dead, 2.0f * dead, 2.0f * dead},
                      FA(tokens.input), dead);
    renderer.StrokeRect({hub.x - dead, hub.y - dead, 2.0f * dead, 2.0f * dead},
                        FA(tokens.border), 1.0f, dead);

    // PieMenuIndicator: a hub needle that turns toward the highlighted
    // wedge the short way.
    if (highlighted_ >= 0 && highlighted_ < n) {
        const float cs = std::cos(indicator_angle_);
        const float sn = std::sin(indicator_angle_);
        renderer.DrawLine(
            {hub.x, hub.y},
            {hub.x + cs * dead * 0.72f, hub.y + sn * dead * 0.72f},
            FA(tokens.accent), 2.5f);
    } else {
        // Resting hub dot.
        renderer.FillRect(
            {hub.x - 2.5f, hub.y - 2.5f, 5.0f, 5.0f},
            FA(tokens.muted), 2.5f);
    }

    // Labels (+ optional icons) at each wedge's centroid.
    const float mid = dead + (r - dead) * 0.55f;
    for (int i = 0; i < n; ++i) {
        const auto& item = items_[static_cast<std::size_t>(i)];
        const float a = ItemAngle(i);
        const float cx = hub.x + std::cos(a) * mid;
        const float cy = hub.y + std::sin(a) * mid;
        Theme::Color text_color = item.danger ? tokens.danger
                                              : tokens.foreground;
        if (!item.enabled) {
            text_color = tokens.disabled;
        }
        const Rendering::TextStyle style{FA(text_color), tokens.font_small,
                                         500};
        const auto sample = item.label.empty()
            ? std::string_view("Ag")
            : std::string_view(item.label);
        const auto measured = renderer.MeasureText(sample, style);
        const bool has_icon = !item.icon.empty() &&
                              static_cast<bool>(g_icon_resolver);
        const float block_h = measured.height +
            (has_icon ? kIconSize + kIconGap : 0.0f);
        float cy_icon = cy - block_h * 0.5f;
        if (has_icon) {
            g_icon_resolver(renderer, item.icon.c_str(),
                            static_cast<int>(kIconSize), FA(text_color),
                            Layout::Rect{cx - kIconSize * 0.5f, cy_icon,
                                         kIconSize, kIconSize});
            cy_icon += kIconSize + kIconGap;
        }
        renderer.DrawText(
            item.label,
            Layout::Rect{cx - measured.width * 0.5f, cy_icon,
                         measured.width + 2.0f, measured.height + 1.0f},
            style);
    }
}

void PieMenu::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool PieMenu::HandleEvent(const Event& event) {
    if (closed_) {
        return false;
    }
    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left ||
                event.mouse_button == MouseButton::Right) {
                // A stale suppress_click_ from a previous drag must not
                // eat this press (same rule as ContextMenu).
                suppress_click_ = false;
                tracking_ = true;
                SetHighlighted(HitWedge(event.position));
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            SetHighlighted(HitWedge(event.position));
            return true;
        case Event::Type::MouseUp:
            // Right-button gestures never capture: when the menu was
            // opened by the very press that still holds (context-menu
            // marking-menu), the MouseDown went to the pre-overlay
            // target and tracking_ stays false — the release is still
            // the gesture end, so commit on right-up unconditionally.
            if (event.mouse_button == MouseButton::Right ||
                (tracking_ && event.mouse_button == MouseButton::Left)) {
                tracking_ = false;
                const int index = HitWedge(event.position);
                if (event.mouse_button == MouseButton::Left) {
                    // The tree synthesizes a Click after this release;
                    // swallow it so the same gesture can't fire twice.
                    suppress_click_ = true;
                }
                // -1/-2 (outside ring aim is still a wedge; only the
                // dead zone yields -2) — Finish cancels on anything but
                // an enabled item.
                Finish(index >= 0 ? index : -1);
                return true;
            }
            return false;
        case Event::Type::Click: {
            if (suppress_click_) {
                suppress_click_ = false;
                return true;
            }
            // Click-outside cancels: a click beyond the ring is a
            // dismiss, not an aim (aim-by-direction only applies while
            // the press-drag-release gesture is tracked).
            const auto hub = Hub();
            const float dx = event.position.x - hub.x;
            const float dy = event.position.y - hub.y;
            if (std::sqrt(dx * dx + dy * dy) > RingOuter()) {
                Cancel();
                return true;
            }
            Finish(HitWedge(event.position));
            return true;
        }
        case Event::Type::KeyDown:
            switch (event.key) {
                case KeyCode::Escape:
                    Cancel();
                    return true;
                case KeyCode::ArrowUp:
                    MoveHighlightByDirection(-kPi * 0.5f);
                    return true;
                case KeyCode::ArrowRight:
                    MoveHighlightByDirection(0.0f);
                    return true;
                case KeyCode::ArrowDown:
                    MoveHighlightByDirection(kPi * 0.5f);
                    return true;
                case KeyCode::ArrowLeft:
                    MoveHighlightByDirection(kPi);
                    return true;
                case KeyCode::Enter:
                case KeyCode::Space:
                    Finish(highlighted_);
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

Layout::Size PieMenu::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size PieMenu::GetPreferredSize() const {
    return {0.0f, 0.0f};
}

Layout::Size PieMenu::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
