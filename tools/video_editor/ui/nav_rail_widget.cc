#include "nav_rail_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "app_runtime/icon_pack.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::VideoEditor::Ui {
namespace {

constexpr float kCell = 38.0f;
constexpr float kGap = 6.0f;
constexpr float kIcon = 18.0f;

// hovered_/pressed_ encoding: >=0 top_ index, <=-2 bottom index
// (-2 - i), -1 none. `~i` would collide bottom[0] with "none".
int EncodeBottom(int i) { return -2 - i; }
int DecodeBottom(int e) { return -2 - e; }
constexpr int kNoPress = -1;

XER::UI::Theme::Color Fade(XER::UI::Theme::Color color, float alpha) {
    color.a = alpha;
    return color;
}

// Theme float color → packed AARRGGBB tint for the icon pack.
std::uint32_t ToArgb(XER::UI::Theme::Color color) {
    const auto byte = [](float v) {
        return static_cast<std::uint32_t>(
            std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    return (byte(color.a) << 24) | (byte(color.r) << 16) |
           (byte(color.g) << 8) | byte(color.b);
}

// Logical icon name per rail glyph (assets/icons.yaml). The Trans slot
// hosts the markers panel in the shell, so it maps to nav.markers.
const char* IconNameFor(NavRailWidget::Glyph glyph) {
    using Glyph = NavRailWidget::Glyph;
    switch (glyph) {
        case Glyph::Media: return "nav.media";
        case Glyph::Audio: return "nav.audio";
        case Glyph::Text: return "nav.text";
        case Glyph::Fx: return "nav.fx";
        case Glyph::Trans: return "nav.markers";
        case Glyph::Settings: return "nav.settings";
    }
    return nullptr;
}

// Rasterized SVG icon from the shared pack, drawn centered in `box`.
// Returns false when the icon is missing or the renderer can't blit
// it — the caller then falls back to the vector glyph.
bool DrawIcon(XER::UI::Rendering::Renderer& renderer, const char* name,
              int px, XER::UI::Theme::Color tint,
              const XER::UI::Layout::Rect& box) {
    const auto* bmp =
        AppRuntime::SharedIconPack().Get(name, px, ToArgb(tint));
    if (bmp == nullptr) return false;
    const XER::UI::Layout::Rect dst{
        box.x + (box.width - bmp->width) * 0.5f,
        box.y + (box.height - bmp->height) * 0.5f,
        static_cast<float>(bmp->width),
        static_cast<float>(bmp->height)};
    return renderer.DrawFrameBgra(bmp->bgra.data(), bmp->width,
                                  bmp->height, dst, 1.0f);
}

}  // namespace

void NavRailWidget::SetItems(std::vector<Item> top,
                             std::vector<Item> bottom) {
    top_.clear();
    bottom_.clear();
    top_.reserve(top.size());
    bottom_.reserve(bottom.size());
    for (auto& item : top) top_.push_back({std::move(item), {}});
    for (auto& item : bottom) bottom_.push_back({std::move(item), {}});
    // Replacing the item vectors invalidates every saved index — a
    // press started under the old list must not resolve into the new
    // one (stale index -> OOB, or worse, the wrong action).
    pressed_ = kNoPress;
    hovered_ = -1;
    Invalidate();
}

void NavRailWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    float y = bounds.y + 10.0f;
    const float x = bounds.x + (bounds.width - kCell) * 0.5f;
    for (auto& slot : top_) {
        slot.rect = {x, y, kCell, kCell};
        y += kCell + kGap;
    }
    float by = bounds.y + bounds.height - 10.0f - kCell;
    for (int i = static_cast<int>(bottom_.size()) - 1; i >= 0; --i) {
        bottom_[i].rect = {x, by, kCell, kCell};
        by -= kCell + kGap;
    }
}

bool NavRailWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    const auto hit = [&](const XER::UI::Layout::Point& p) -> int {
        for (std::size_t i = 0; i < top_.size(); ++i) {
            if (top_[i].rect.Contains(p)) return static_cast<int>(i);
        }
        for (std::size_t i = 0; i < bottom_.size(); ++i) {
            if (bottom_[i].rect.Contains(p)) {
                return EncodeBottom(static_cast<int>(i));
            }
        }
        return -1;
    };

    switch (event.type) {
        case Event::Type::MouseMove: {
            const int h = hit(event.position);
            if (h != hovered_) {
                hovered_ = h;
                // Surface the item's hint through the widget tooltip.
                if (h == -1) {
                    SetTooltip("");
                } else {
                    const Item& item = h >= 0
                        ? top_[static_cast<std::size_t>(h)].item
                        : bottom_[static_cast<std::size_t>(
                              DecodeBottom(h))]
                              .item;
                    SetTooltip(item.tooltip);
                }
                Invalidate();
            }
            return hovered_ != -1;
        }
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                pressed_ = hit(event.position);
                Invalidate();
                return pressed_ != -1;
            }
            return false;
        case Event::Type::MouseUp:
            return pressed_ != kNoPress;
        case Event::Type::Click: {
            const int h = hit(event.position);
            if (pressed_ != kNoPress && h == pressed_) {
                const int was = pressed_;
                pressed_ = kNoPress;
                // Bounds-check the decoded index — SetItems may have
                // shrunk the vectors since the press landed.
                const Item* item = nullptr;
                if (was >= 0) {
                    if (static_cast<std::size_t>(was) < top_.size()) {
                        item = &top_[static_cast<std::size_t>(was)].item;
                    }
                } else {
                    const int bi = DecodeBottom(was);
                    if (bi >= 0 &&
                        static_cast<std::size_t>(bi) < bottom_.size()) {
                        item =
                            &bottom_[static_cast<std::size_t>(bi)].item;
                    }
                }
                if (item != nullptr && item->action) item->action();
                Invalidate();
                return true;
            }
            pressed_ = kNoPress;
            return true;
        }
        default:
            return false;
    }
}

void NavRailWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.panel, 0.0f);
    renderer.DrawLine({bounds.x + bounds.width, bounds.y},
                      {bounds.x + bounds.width,
                       bounds.y + bounds.height},
                      tokens.border, 1.0f);

    const auto draw_slot = [&](const Slot& slot, int encoded) {
        const bool active =
            slot.item.is_active && slot.item.is_active();
        const bool hovered = hovered_ == encoded;
        const bool pressed = pressed_ == encoded;
        if (active || hovered || pressed) {
            const auto fill = active
                ? Fade(tokens.accent, 0.22f)
                : Fade(tokens.hover, pressed ? 0.9f : 0.6f);
            renderer.FillRect(slot.rect, fill, tokens.radius_medium);
        }
        if (active) {
            // Accent bar on the slot's left edge — the selected-section
            // indicator, Premiere/VS Code activity-bar style.
            renderer.FillRect({slot.rect.x + 1.0f, slot.rect.y + 9.0f,
                               3.0f, slot.rect.height - 18.0f},
                              tokens.accent, 1.5f);
        }
        const auto color =
            active ? tokens.accent
                   : (hovered ? tokens.foreground : tokens.muted);
        DrawGlyph(renderer, tokens, slot.item.glyph, slot.rect, color);
    };

    for (std::size_t i = 0; i < top_.size(); ++i) {
        draw_slot(top_[i], static_cast<int>(i));
    }
    for (std::size_t i = 0; i < bottom_.size(); ++i) {
        draw_slot(bottom_[i], EncodeBottom(static_cast<int>(i)));
    }
}

void NavRailWidget::DrawGlyph(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens, Glyph glyph,
    const XER::UI::Layout::Rect& r, XER::UI::Theme::Color color) const {
    // Prefer the SVG asset when the pack has one for this slot, tinted
    // with the same state color the vector glyph would use; the drawn
    // glyphs below stay as the fallback.
    if (const char* icon = IconNameFor(glyph);
        icon != nullptr && DrawIcon(renderer, icon, 20, color, r)) {
        return;
    }
    const float cx = r.x + r.width * 0.5f;
    const float cy = r.y + r.height * 0.5f;
    const float h = kIcon * 0.5f;
    switch (glyph) {
        case Glyph::Media: {
            // Film strip: rounded frame + two sprocket columns.
            const XER::UI::Layout::Rect fr{cx - h, cy - h * 0.7f,
                                           kIcon, kIcon * 0.7f};
            renderer.StrokeRect(fr, color, 1.4f, 2.0f);
            renderer.DrawLine({fr.x + 5.0f, fr.y},
                              {fr.x + 5.0f, fr.y + fr.height}, color,
                              1.2f);
            renderer.DrawLine({fr.x + fr.width - 5.0f, fr.y},
                              {fr.x + fr.width - 5.0f,
                               fr.y + fr.height},
                              color, 1.2f);
            break;
        }
        case Glyph::Audio: {
            // Waveform bars.
            const float bars[] = {4.0f, 9.0f, 14.0f, 9.0f, 4.0f};
            const float bw = 2.4f;
            float x = cx - 9.0f;
            for (const float b : bars) {
                renderer.FillRect({x, cy - b * 0.5f, bw, b}, color, 1.0f);
                x += 4.4f;
            }
            break;
        }
        case Glyph::Text:
            renderer.DrawText("T",
                              {r.x, r.y + 1.0f, r.width, r.height},
                              {color, tokens.font_title, 700});
            break;
        case Glyph::Fx: {
            // Sparkle: four-point star from two crossing lines + dot.
            renderer.DrawLine({cx, cy - h}, {cx, cy + h}, color, 1.4f);
            renderer.DrawLine({cx - h, cy}, {cx + h, cy}, color, 1.4f);
            renderer.DrawLine({cx - h * 0.5f, cy - h * 0.5f},
                              {cx + h * 0.5f, cy + h * 0.5f}, color,
                              1.1f);
            renderer.DrawLine({cx + h * 0.5f, cy - h * 0.5f},
                              {cx - h * 0.5f, cy + h * 0.5f}, color,
                              1.1f);
            break;
        }
        case Glyph::Trans: {
            // Crossfade: two overlapping chevrons.
            renderer.DrawLine({cx - h, cy + h * 0.6f},
                              {cx - h * 0.1f, cy - h * 0.6f}, color,
                              1.5f);
            renderer.DrawLine({cx - h * 0.1f, cy - h * 0.6f},
                              {cx + h, cy + h * 0.6f}, color, 1.5f);
            renderer.DrawLine({cx - h * 0.55f, cy + h * 0.15f},
                              {cx + h * 0.55f, cy + h * 0.15f}, color,
                              1.0f);
            break;
        }
        case Glyph::Settings: {
            // Gear: circle + four spokes.
            const float rad = h * 0.55f;
            const int segs = 10;
            for (int i = 0; i < segs; ++i) {
                const float a0 = i * 6.28318f / segs;
                const float a1 = (i + 1) * 6.28318f / segs;
                renderer.DrawLine(
                    {cx + rad * std::cos(a0), cy + rad * std::sin(a0)},
                    {cx + rad * std::cos(a1), cy + rad * std::sin(a1)},
                    color, 1.4f);
            }
            for (int i = 0; i < 4; ++i) {
                const float a = i * 1.5708f;
                renderer.DrawLine(
                    {cx + (rad + 1.5f) * std::cos(a),
                     cy + (rad + 1.5f) * std::sin(a)},
                    {cx + (rad + 4.5f) * std::cos(a),
                     cy + (rad + 4.5f) * std::sin(a)},
                    color, 1.6f);
            }
            break;
        }
    }
    (void)tokens;
}

}  // namespace XER::VideoEditor::Ui
