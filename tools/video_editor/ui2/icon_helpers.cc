#include "icon_helpers.h"

#include <algorithm>
#include <cmath>

#include "app_runtime/icon_pack.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/sidebar.h"
#include "widgets/tabs.h"

namespace XER::VideoEditor::Ui2::VeIcons {

std::uint32_t ToArgb(Color color) {
    const auto byte = [](float v) {
        return static_cast<std::uint32_t>(
            std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    return (byte(color.a) << 24) | (byte(color.r) << 16) |
           (byte(color.g) << 8) | byte(color.b);
}

bool DrawIcon(XER::UI::Rendering::Renderer& renderer, const char* name,
              int px, Color tint,
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

bool DrawIcon(XER::UI::Rendering::Renderer& renderer, float x, float y,
              float size, const char* name, std::uint32_t argb) {
    const auto* bmp = AppRuntime::SharedIconPack().Get(
        name, static_cast<int>(size), argb);
    if (bmp == nullptr) return false;
    const XER::UI::Layout::Rect box{x, y, size, size};
    const XER::UI::Layout::Rect dst{
        box.x + (box.width - bmp->width) * 0.5f,
        box.y + (box.height - bmp->height) * 0.5f,
        static_cast<float>(bmp->width),
        static_cast<float>(bmp->height)};
    return renderer.DrawFrameBgra(bmp->bgra.data(), bmp->width,
                                  bmp->height, dst, 1.0f);
}

bool DrawToggleIcon(XER::UI::Rendering::Renderer& renderer, bool on,
                    const char* on_name, const char* off_name, int px,
                    Color tint, const XER::UI::Layout::Rect& box) {
    return DrawIcon(renderer, on ? on_name : off_name, px, tint, box);
}

void InstallButtonIconResolver() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    const auto resolve =
        [](XER::UI::Rendering::Renderer& renderer, const char* name,
           int px, Color tint, const XER::UI::Layout::Rect& box) {
            return DrawIcon(renderer, name, px, tint, box);
        };
    XER::UI::Widgets::Button::SetIconResolver(resolve);
    XER::UI::Widgets::Tabs::SetIconResolver(resolve);
    XER::UI::Widgets::Sidebar::SetIconResolver(resolve);
}

void DrawChevron(XER::UI::Rendering::Renderer& renderer, Color color,
                 const XER::UI::Layout::Rect& box, bool collapsed) {
    // A "v" (expanded) / ">" (collapsed) made of two strokes centered
    // in the slot — no filled-path API exists, so strokes it is.
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float arm = box.width * 0.28f;
    if (collapsed) {
        renderer.DrawLine({cx - arm * 0.5f, cy - arm},
                          {cx + arm * 0.5f, cy}, color, 1.4f);
        renderer.DrawLine({cx + arm * 0.5f, cy},
                          {cx - arm * 0.5f, cy + arm}, color, 1.4f);
    } else {
        renderer.DrawLine({cx - arm, cy - arm * 0.5f}, {cx, cy + arm * 0.5f},
                          color, 1.4f);
        renderer.DrawLine({cx, cy + arm * 0.5f},
                          {cx + arm, cy - arm * 0.5f}, color, 1.4f);
    }
}

XER::UI::Layout::Rect DrawSectionHeader(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const XER::UI::Layout::Rect& rect, std::string_view label,
    bool collapsed) {
    const float slot = rect.height;
    const float chevron_box = std::min(slot, 14.0f);
    DrawChevron(renderer, tokens.muted,
                {rect.x, rect.y + (rect.height - chevron_box) * 0.5f,
                 chevron_box, chevron_box},
                collapsed);
    const XER::UI::Layout::Rect label_rect{
        rect.x + slot + Theme::kSpaceXs, rect.y,
        std::max(0.0f, rect.width - slot - Theme::kSpaceXs),
        rect.height};
    renderer.DrawText(
        label, label_rect,
        XER::UI::Rendering::TextStyle{tokens.muted, tokens.font_small,
                                      600});
    return label_rect;
}

}  // namespace XER::VideoEditor::Ui2::VeIcons
