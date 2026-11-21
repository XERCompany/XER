// Shared IconPack draw helpers for the video editor UI — the
// rasterize-and-blit path that used to be copy-pasted into
// effects_panel.cc, plugins_panel_widget.cc and nav_rail_widget.cc.
// Every icon goes through the AppRuntime::SharedIconPack() cache:
// logical name (assets/icons.yaml) -> SVG -> tinted BGRA bitmap ->
// Renderer::DrawFrameBgra.
#pragma once

#include <cstdint>
#include <string_view>

#include "layout/geometry.h"
#include "theme/color.h"

namespace XER::UI::Rendering {
class Renderer;
}
namespace XER::UI::Theme {
struct Tokens;
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

// Toggle-state icon: draws `on_name` when `on`, `off_name` otherwise —
// the track-header pattern (track.lock/track.unlock,
// track.visible/track.hidden, track.mute/track.unmute). Same blit path
// and return contract as DrawIcon.
bool DrawToggleIcon(XER::UI::Rendering::Renderer& renderer, bool on,
                    const char* on_name, const char* off_name, int px,
                    Color tint, const XER::UI::Layout::Rect& box);

// ---- collapsible section-header chrome ----
// The Premiere-style section pattern shared by the right-dock panels:
// a small chevron (pointing down when expanded, right when collapsed)
// followed by a bold caption label.

// Chevron glyph only — two strokes inside `box` (a square slot).
// `collapsed` draws the right-pointing variant.
void DrawChevron(XER::UI::Rendering::Renderer& renderer, Color color,
                 const XER::UI::Layout::Rect& box, bool collapsed);

// Full header row: chevron slot + `label` in muted/small/600. Returns
// the rect the label occupies (everything after the chevron slot) so
// callers can align trailing content against it.
XER::UI::Layout::Rect DrawSectionHeader(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const XER::UI::Layout::Rect& rect, std::string_view label,
    bool collapsed);

}  // namespace XER::VideoEditor::Ui::VeIcons
