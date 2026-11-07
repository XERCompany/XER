#pragma once

#include <cstdint>
#include <string>

namespace XER::App::NodeGraphApp {

// Registers application-level node types into the shared NodeGraph registry:
// audio file import/export (WAV), Lua buffer scripting and VST3 plugin
// processing. Call once before the editor loads graphs.
void RegisterAppNodes();

// Opens the VST3 editor view for the plugin hosted by the given node.
// parent_hwnd is the top-level window; canvas_rect is the node-graph canvas
// area in DIPs (96-dpi units) — converted to pixels internally.
// Returns false when the node has no loaded plugin or no editor view.
bool OpenPluginEditor(std::uint64_t node_id, void* parent_hwnd,
                      float canvas_x, float canvas_y,
                      float canvas_w, float canvas_h,
                      std::string* error_out = nullptr);

// Detaches every open plugin editor view (app shutdown / graph close).
void CloseAllPluginEditors();

}  // namespace XER::App::NodeGraphApp
