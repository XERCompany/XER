// Native modules returned by runtime.js's NativeImport(name).
// Three modules implement the bootstrap surface:
//
//   app_super_winkernel
//     .format(a, b, ...)                     → string concat
//     .SiEnvironmentMatchPath("GET", path)   → expands %ENV% vars in
//                                              path (%WEAKLY_ROOT_PATH_EXECUTE%
//                                              maps to the exe dir)
//
//   app_super_concat
//     .handle({join: "/"}, a, b, ...)        → join args with separator
//
//   app_super_exportAppData
//     .ENUM.CONFIG_WINDOW                    → "config.window"
//     .ENUM.FUNCTION.HANDLER_EVENT_LOAD_PLUGIN → "handler.plugin_load"
//     .setOutAppData(obj)                    → stores key→value into the
//                                              engine's out-data registry
//
//   app_super_net
//     .fetch(url [, opts])                   → {status, body, headers,
//                                              ok, error} — opts:
//                                              {method, body, headers,
//                                              timeout_ms}
//     .download(url, path [, timeout_ms])    → {ok, path} or
//                                              {ok:false, error}
//
// RegisterAll installs all four on the engine so NativeImport resolves.
#pragma once

#include <string>

namespace XER::VideoEditor::AppRuntime {

class JsEngine;

// Expands %VAR% tokens in `input` from the process environment;
// %WEAKLY_ROOT_PATH_EXECUTE% always resolves to the executable's
// directory (overrides the real env var).
std::string ExpandEnvPath(const std::string& input);

// Installs app_super_winkernel, app_super_concat and
// app_super_exportAppData module factories on the engine.
void RegisterAll(JsEngine& engine);

// Implemented in export_appdata.cc; called by RegisterAll.
void RegisterExportAppData(JsEngine& engine);

// Out-data key constants (must match the JS ENUM values above).
inline constexpr const char* kConfigWindow = "config.window";
inline constexpr const char* kConfigIcons = "config.icons";
inline constexpr const char* kConfigEditor = "config.editor";
inline constexpr const char* kConfigShortcuts = "config.shortcuts";
inline constexpr const char* kConfigIconsDir = "config.icons_dir";
inline constexpr const char* kConfigToolbar = "config.toolbar";
inline constexpr const char* kConfigStrings = "config.strings";
inline constexpr const char* kConfigTheme = "config.theme";
inline constexpr const char* kConfigLayout = "config.layout";
inline constexpr const char* kConfigPanels = "config.panels";
inline constexpr const char* kConfigWorkspaces = "config.workspaces";
inline constexpr const char* kConfigColor = "config.color";
inline constexpr const char* kHandlerPluginLoad = "handler.plugin_load";

}  // namespace XER::VideoEditor::AppRuntime
