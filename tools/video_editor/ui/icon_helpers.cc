#include "icon_helpers.h"

#include <algorithm>
#include <cmath>

#include "app_runtime/icon_pack.h"
#include "rendering/renderer.h"
#include "widgets/button.h"

namespace XER::VideoEditor::Ui::VeIcons {

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

void InstallButtonIconResolver() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    XER::UI::Widgets::Button::SetIconResolver(
        [](XER::UI::Rendering::Renderer& renderer, const char* name,
           int px, Color tint, const XER::UI::Layout::Rect& box) {
            return DrawIcon(renderer, name, px, tint, box);
        });
}

}  // namespace XER::VideoEditor::Ui::VeIcons
