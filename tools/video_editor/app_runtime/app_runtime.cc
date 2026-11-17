// Application bootstrap — implementation of app_runtime.h.
//
// Boot() resolves the assets directory (<exe_dir>/assets first, then
// CWD/assets), parses app.yaml, creates the JsEngine, registers the
// native modules, executes the runtime script and collects the
// config.window out-data (a window.yaml path). Failures before the
// engine exists leave BootResult::window at compiled-in defaults so
// the app still starts. FirePluginLoad() forwards plugin descriptors
// to a registered handler.plugin_load JS callback — always optional,
// never fatal.
#include "app_runtime.h"

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#include "color/ocio_transform.h"
#include "icon_pack.h"
#include "js_engine.h"
#include "native_modules.h"
#include "ui_manifest.h"

namespace XER::VideoEditor::AppRuntime {
namespace {

std::string PathToUtf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path Utf8ToPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::filesystem::path ExeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4]{};
    const DWORD n =
        GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                                        nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), utf8.data(),
                        len, nullptr, nullptr);
    return Utf8ToPath(utf8).parent_path();
#else
    char buf[PATH_MAX]{};
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return {};
    }
    return std::filesystem::path(std::string(buf, static_cast<size_t>(n)))
        .parent_path();
#endif
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

JsEngine*& SharedBootEngine() {
    static JsEngine* engine = nullptr;
    return engine;
}

AssetsConfig& SharedAssetsConfig() {
    static AssetsConfig cfg;
    return cfg;
}

BootResult Boot(std::string* status) {
    BootResult result;
    std::string err;

    // 1. Locate the assets dir: <exe_dir>/assets, then CWD/assets.
    std::error_code ec;
    std::filesystem::path assets;
    const std::filesystem::path exe_dir = ExeDir();
    if (!exe_dir.empty() &&
        std::filesystem::exists(exe_dir / "assets" / "app.yaml", ec)) {
        assets = exe_dir / "assets";
    } else if (const std::filesystem::path cwd_assets =
                   std::filesystem::current_path(ec) / "assets";
               std::filesystem::exists(cwd_assets / "app.yaml", ec)) {
        assets = cwd_assets;
    }
    if (assets.empty()) {
        result.error = "assets/app.yaml not found";
        return result;  // window keeps compiled-in defaults
    }

    // 2. Manifest.
    if (!LoadAppManifest(assets, &result.manifest, &err)) {
        result.error = err;
        return result;
    }

    // 3. Engine — process-long lifetime; ownership moves to the caller.
    result.engine = JsEngine::Create(&err);
    if (!result.engine) {
        result.error = "js engine: " + err;
        return result;
    }

    // 4. Native modules.
    RegisterAll(*result.engine);

    // 5. Runtime script (manifest value may start with "./").
    const std::filesystem::path script =
        result.manifest.base_dir /
        Utf8ToPath(result.manifest.runtime_script);
    if (!result.engine->RunFile(PathToUtf8(script), &err)) {
        result.error = "runtime.js: " + err;
        return result;
    }

    // 6. Window config. config.window is a window.yaml path — absolute
    // (runtime.js ran it through ExpandEnvPath) or relative to base_dir.
    // window.yaml failures are non-fatal: defaults stay, note to status.
    std::string window_note;
    const OutValue window_out = result.engine->GetOutData(kConfigWindow);
    if (window_out.kind == OutValue::Kind::String && !window_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(window_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        WindowConfig cfg;
        std::string load_err;
        if (LoadWindowConfig(yaml, &cfg, &load_err)) {
            result.window = cfg;
        } else {
            window_note = "window.yaml ignored: " + load_err;
        }
    }

    // 6b. Editor assets — editor.yaml, shortcuts.yaml, icons.yaml and
    // the icons directory, all declared as out-data paths resolved like
    // window.yaml (absolute, or relative to base_dir). Everything here
    // is non-fatal: a missing/bad yaml leaves the defaults and appends
    // a note to the boot status.
    std::string assets_note;
    const auto add_note = [&assets_note](const std::string& note) {
        if (!assets_note.empty()) {
            assets_note += "; ";
        }
        assets_note += note;
    };
    AssetsConfig& assets_cfg = SharedAssetsConfig();
    assets_cfg.assets_dir = result.manifest.base_dir;

    const OutValue editor_out = result.engine->GetOutData(kConfigEditor);
    if (editor_out.kind == OutValue::Kind::String && !editor_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(editor_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        std::string note_err;
        if (!LoadEditorConfig(yaml, &assets_cfg.editor, &note_err)) {
            add_note("editor.yaml ignored: " + note_err);
        }
    }

    const OutValue shortcuts_out =
        result.engine->GetOutData(kConfigShortcuts);
    if (shortcuts_out.kind == OutValue::Kind::String &&
        !shortcuts_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(shortcuts_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        std::string note_err;
        if (!LoadShortcutsConfig(yaml, &assets_cfg.shortcuts, &note_err)) {
            add_note("shortcuts.yaml ignored: " + note_err);
        }
    }

    const OutValue icons_out = result.engine->GetOutData(kConfigIcons);
    if (icons_out.kind == OutValue::Kind::String && !icons_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(icons_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        std::string note_err;
        if (!LoadIconsManifest(yaml, &assets_cfg.icon_map, &note_err)) {
            add_note("icons.yaml ignored: " + note_err);
        }
    }

    const OutValue icons_dir_out =
        result.engine->GetOutData(kConfigIconsDir);
    if (icons_dir_out.kind == OutValue::Kind::String &&
        !icons_dir_out.str.empty()) {
        std::filesystem::path dir = Utf8ToPath(icons_dir_out.str);
        if (dir.is_relative()) {
            dir = result.manifest.base_dir / dir;
        }
        assets_cfg.icons_dir = dir;
    }

    // Configure unconditionally: even when icons.yaml failed, icons_dir
    // alone lets the "<name>.svg" fallback resolve unmapped names.
    SharedIconPack().Configure(assets_cfg.icons_dir, assets_cfg.icon_map);

    // UI manifest files — toolbar.json, strings.ini, theme.toml and
    // layout.yaml feed the shared UI state; color.yaml only gets its
    // path recorded (the color module loads it lazily via OCIO).
    const OutValue toolbar_out = result.engine->GetOutData(kConfigToolbar);
    if (toolbar_out.kind == OutValue::Kind::String &&
        !toolbar_out.str.empty()) {
        std::filesystem::path json = Utf8ToPath(toolbar_out.str);
        if (json.is_relative()) {
            json = result.manifest.base_dir / json;
        }
        std::vector<ToolbarButton> buttons;
        std::string note_err;
        if (LoadToolbarJson(json, &buttons, &note_err)) {
            // Keep only the action ids, in file order — separators carry
            // no action and are skipped. The button vector itself is
            // re-loadable, so AssetsConfig stores ids only.
            for (const ToolbarButton& button : buttons) {
                if (!button.action.empty()) {
                    assets_cfg.toolbar_ids.push_back(button.action);
                }
            }
        } else {
            add_note("toolbar.json ignored: " + note_err);
        }
    }

    const OutValue strings_out = result.engine->GetOutData(kConfigStrings);
    if (strings_out.kind == OutValue::Kind::String &&
        !strings_out.str.empty()) {
        std::filesystem::path ini = Utf8ToPath(strings_out.str);
        if (ini.is_relative()) {
            ini = result.manifest.base_dir / ini;
        }
        std::string note_err;
        if (!LoadStringsIni(ini, &SharedUiStrings(), &note_err)) {
            add_note("strings.ini ignored: " + note_err);
        }
    }

    const OutValue theme_out = result.engine->GetOutData(kConfigTheme);
    if (theme_out.kind == OutValue::Kind::String && !theme_out.str.empty()) {
        std::filesystem::path toml = Utf8ToPath(theme_out.str);
        if (toml.is_relative()) {
            toml = result.manifest.base_dir / toml;
        }
        std::string note_err;
        if (!LoadThemeToml(toml, &SharedThemeOverride(), &note_err)) {
            add_note("theme.toml ignored: " + note_err);
        }
    }

    const OutValue layout_out = result.engine->GetOutData(kConfigLayout);
    if (layout_out.kind == OutValue::Kind::String &&
        !layout_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(layout_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        std::string note_err;
        if (!LoadLayoutYaml(yaml, &SharedLayoutConfig(), &note_err)) {
            add_note("layout.yaml ignored: " + note_err);
        }
    }

    // panels.yaml feeds SharedPanelsConfig; workspaces.yaml fills
    // SharedWorkspacesConfig — both via the same out-data path pattern.
    // The panels loader also reports soft warnings (unknown docks)
    // through its error string on a true return, so a successful load
    // can still earn a note.
    const OutValue panels_out = result.engine->GetOutData(kConfigPanels);
    if (panels_out.kind == OutValue::Kind::String &&
        !panels_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(panels_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        std::string note_err;
        if (!LoadPanelsConfig(yaml, &SharedPanelsConfig(), &note_err)) {
            add_note("panels.yaml ignored: " + note_err);
        } else if (!note_err.empty()) {
            add_note("panels.yaml: " + note_err);
        }
    }

    const OutValue workspaces_out =
        result.engine->GetOutData(kConfigWorkspaces);
    if (workspaces_out.kind == OutValue::Kind::String &&
        !workspaces_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(workspaces_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        std::string note_err;
        if (!LoadWorkspacesConfig(yaml, &SharedWorkspacesConfig(),
                                  &note_err)) {
            add_note("workspaces.yaml ignored: " + note_err);
        }
    }

    const OutValue color_out = result.engine->GetOutData(kConfigColor);
    if (color_out.kind == OutValue::Kind::String && !color_out.str.empty()) {
        std::filesystem::path yaml = Utf8ToPath(color_out.str);
        if (yaml.is_relative()) {
            yaml = result.manifest.base_dir / yaml;
        }
        // Path only — the color module resolves/loads it on first use.
        assets_cfg.color_yaml = PathToUtf8(yaml);

        // Named OCIO profiles from the same file — the loader is
        // yaml-only, so this runs with or without the OCIO runtime.
        // An absent Profiles section is silent (loader returns true
        // with an empty list); only missing/malformed yaml is noted.
        std::string note_err;
        if (!Color::LoadOcioProfiles(yaml, &Color::SharedOcioProfiles(),
                                     &note_err)) {
            add_note("color profiles ignored: " + note_err);
        }
    }

    // 7. Done — publish the engine for long-lived subsystems.
    SharedBootEngine() = result.engine.get();
    result.ok = true;
    if (status != nullptr) {
        if (!status->empty()) {
            *status += "; ";
        }
        *status += "assets: loaded " + PathToUtf8(script.filename()) +
                   " (window " + std::to_string(result.window.width) + "x" +
                   std::to_string(result.window.height) + " \"" +
                   result.window.title + "\")";
        if (!window_note.empty()) {
            *status += "; " + window_note;
        }
        if (!assets_note.empty()) {
            *status += "; " + assets_note;
        }
    }
    return result;
}

void FirePluginLoad(JsEngine* engine, const std::string& dll_path,
                    const std::string& name, const std::string& version,
                    const std::string& features) {
    if (engine == nullptr) {
        return;
    }
    const OutValue handler = engine->GetOutData(kHandlerPluginLoad);
    if (handler.kind != OutValue::Kind::Handler) {
        return;  // handler optional
    }

    // The payload {path, factory:{name,version,features}} is nested, but
    // MakeObjectArg only produces flat objects — so the JSON text is built
    // here and passed as a Kind::Json OutValue, which CallHandler parses
    // back into a JS object. Defensive: Kind::Json extends the bootstrap
    // OutValue contract (js_engine.cc adds it for exactly this case); if
    // it is absent this file fails to compile rather than silently
    // degrading the argument into a string.
    OutValue arg;
    arg.kind = OutValue::Kind::Json;
    arg.str = "{\"path\":\"" + JsonEscape(dll_path) +
              "\",\"factory\":{\"name\":\"" + JsonEscape(name) +
              "\",\"version\":\"" + JsonEscape(version) +
              "\",\"features\":\"" + JsonEscape(features) + "\"}}";

    std::string err;
    // Plugin load must not crash on handler exceptions — the engine
    // reports them internally; a false return is ignored by design.
    (void)engine->CallHandler(kHandlerPluginLoad, {arg}, &err);
}

}  // namespace XER::VideoEditor::AppRuntime
