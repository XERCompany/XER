// Application bootstrap: turns the assets/ directory into runtime state.
//
//   Boot() resolves <exe>/assets/app.yaml → reads the manifest → creates
//   the JsEngine → registers native modules → executes RuntimeScript →
//   collects out-data (window yaml path, event handlers).
//
// The WindowConfig is then applied by main.cc when creating the native
// window; registered handlers (e.g. handler.plugin_load) are invoked by
// the rest of the app via CallPluginLoad() etc.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "app_manifest.h"
#include "editor_config.h"

namespace XER::VideoEditor::AppRuntime {

class JsEngine;

struct BootResult {
    bool ok = false;
    std::string error;
    AppManifest manifest;
    WindowConfig window;
    // Present when app.yaml declared a runtime script that executed and
    // produced out-data; nullptr when the assets layer is absent —
    // callers fall back to compiled-in defaults.
    std::unique_ptr<JsEngine> engine;
};

// Locates the assets directory: <exe_dir>/assets, then CWD/assets.
// Returns an error result when neither exists — the app still boots
// with defaults (assets layer is optional at runtime).
// On success Boot also stores the engine in SharedBootEngine() so
// long-lived subsystems (plugin host) can fire JS handlers.
BootResult Boot(std::string* status);

// Process-wide pointer to the booted engine (nullptr before Boot or
// when the assets layer is absent). Ownership stays with the
// BootResult the caller keeps — main.cc stashes it in a static.
JsEngine*& SharedBootEngine();

// Fires handler.plugin_load (if registered) with the plugin descriptor
// fields the plugin host knows: {path, factory:{name,version,features}}.
// No-op when no engine/handler exists. Called by plugin_host after a
// successful DLL registration.
void FirePluginLoad(JsEngine* engine, const std::string& dll_path,
                    const std::string& name, const std::string& version,
                    const std::string& features);

// Everything Boot() resolved beyond the window config — editor
// settings, icon mapping, shortcuts, UI-manifest buttons, OCIO.
// Populated even when only some files exist; empty when the assets
// layer is absent.
struct AssetsConfig {
    std::filesystem::path assets_dir;      // resolved <exe>/assets
    EditorConfig editor;
    ShortcutsConfig shortcuts;
    std::unordered_map<std::string, std::string> icon_map;
    std::filesystem::path icons_dir;
    std::vector<std::string> toolbar_ids;  // action ids, in file order
    std::string color_yaml;                // resolved color.yaml path
};

// Process-wide assets config filled by Boot(). Widgets read it for
// theming/defaults — empty struct means "compiled defaults".
AssetsConfig& SharedAssetsConfig();

}  // namespace XER::VideoEditor::AppRuntime
