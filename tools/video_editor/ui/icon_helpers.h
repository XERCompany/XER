// Shared IconPack draw helpers for the video editor UI — the
// rasterize-and-blit path that used to be copy-pasted into
// effects_panel.cc, plugins_panel_widget.cc and nav_rail_widget.cc.
// Every icon goes through the AppRuntime::SharedIconPack() cache:
// logical name (assets/icons.yaml) -> SVG -> tinted BGRA bitmap ->
// Renderer::DrawFrameBgra.
#pragma once

#include <cstdint>

#include "layout/geometry.h"
#include "theme/color.h"

namespace XER::UI::Rendering {
class Renderer;
}

namespace XER::VideoEditor::Ui::VeIcons {

using XER::UI::Theme::Color;

// Theme float color -> packed AARRGGBB tint for IconPack::Get.
std::uint32_t ToArgb(Color color);

// Rasterized SVG icon from the shared pack, drawn centered in `box`
// (the icon keeps its rasterized `px` size; the box only positions it).
// Returns false when the icon is missing or the renderer can't blit
// it — callers keep their text/vector-glyph fallback then.
bool DrawIcon(XER::UI::Rendering::Renderer& renderer, const char* name,
              int px, Color tint, const XER::UI::Layout::Rect& box);

// Convenience form: rasterize `name` at `size`x`size` px tinted by the
// packed AARRGGBB `argb`, drawn into the {x,y,size,size} box.
bool DrawIcon(XER::UI::Rendering::Renderer& renderer, float x, float y,
              float size, const char* name, std::uint32_t argb);

// Installs DrawIcon as Widgets::Button's process-wide icon resolver so
// Button::SetIcon("...") picks up pack icons. Idempotent — call lazily
// wherever icon buttons are built (boot needs no ordering).
void InstallButtonIconResolver();

}  // namespace XER::VideoEditor::Ui::VeIcons
