// App-runtime suite — the assets-layer bootstrap in
// tools/video_editor/app_runtime/: app.yaml/window.yaml parsing
// (app_manifest.h), %ENV% expansion + NativeImport module registry
// (native_modules.h), the embedded V8 JsEngine + out-data registry
// (js_engine.h), and the Boot()/FirePluginLoad() glue
// (app_runtime.h).
//
// Engine cases share one lazily created JsEngine — the V8 platform
// init is process-global, so repeated Create() calls are avoided.
// When the V8 runtime is absent next to the test exe, Create()
// legitimately fails and the engine cases fail (not crash): the
// app-runtime deployment puts libv8 next to the test binary the same
// way it sits next to video_editor.exe.
//
// Boot() is only exercised on the failure path: <exe>/assets and
// CWD/assets both resolve relative to deployment, and the test binary
// is never deployed with assets — so from an empty CWD Boot must
// return ok==false with compiled-in WindowConfig defaults.
//
// Temp YAML fixtures live under
// std::filesystem::temp_directory_path()/xer_apprt_test_<pid> and are
// removed at the end of the suite. NOT registered in
// tests/CMakeLists.txt — the integration patch wires it into
// XER_VideoEditor_Tests (RunAppRuntimeTests + add the source file).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "video_editor_test_util.h"
#include "app_runtime/app_manifest.h"
#include "app_runtime/js_engine.h"
#include "app_runtime/native_modules.h"
#include "app_runtime/app_runtime.h"
#include "app_runtime/editor_config.h"
#include "app_runtime/icon_pack.h"
#include "app_runtime/svg_raster.h"
#include "app_runtime/ui_manifest.h"
#include "color/ocio_transform.h"
#include "io/oiio_image.h"

namespace {
using XER::VideoEditor::AppRuntime::AppManifest;
using XER::VideoEditor::AppRuntime::BootResult;
using XER::VideoEditor::AppRuntime::EditorConfig;
using XER::VideoEditor::AppRuntime::IconBitmap;
using XER::VideoEditor::AppRuntime::IconPack;
using XER::VideoEditor::AppRuntime::JsEngine;
using XER::VideoEditor::AppRuntime::OutValue;
using XER::VideoEditor::AppRuntime::ShortcutsConfig;
using XER::VideoEditor::AppRuntime::WindowConfig;
namespace apprt = XER::VideoEditor::AppRuntime;
namespace vecolor = XER::VideoEditor::Color;
namespace veio = XER::VideoEditor::Io;

// Per-process scratch directory for the YAML fixtures written below.
std::filesystem::path TempRoot() {
#ifdef _WIN32
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(getpid());
#endif
    return std::filesystem::temp_directory_path() /
           ("xer_apprt_test_" + std::to_string(pid));
}

bool WriteFile(const std::filesystem::path& path,
               const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return f.good();
}

// Restores the process CWD on destruction — Boot() consults
// CWD/assets, so the missing-assets case runs from an empty dir.
struct CwdGuard {
    std::filesystem::path prev = std::filesystem::current_path();
    ~CwdGuard() {
        std::error_code ec;
        std::filesystem::current_path(prev, ec);
    }
};

// Shared engine: V8 platform init is process-global, so a single
// lazily created instance backs every engine case.
JsEngine* SharedEngine() {
    static std::string create_error;
    static std::unique_ptr<JsEngine> engine =
        JsEngine::Create(&create_error);
    return engine.get();
}

// ------------------------------------------------------------------
// Cases
// ------------------------------------------------------------------

// LoadAppManifest on a directory with no app.yaml must fail with a
// non-empty error string.
void TestManifestMissingFile() {
    const auto dir = TempRoot() / "missing";
    std::filesystem::create_directories(dir);
    AppManifest manifest;
    std::string error;
    XER_VE_CHECK(!apprt::LoadAppManifest(dir, &manifest, &error));
    XER_VE_CHECK(!error.empty());
}

// A minimal app.yaml declares only Application.RuntimeScript; the
// manifest must report it verbatim and record base_dir.
void TestManifestParsesRuntimeScript() {
    const auto dir = TempRoot() / "ok";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(dir / "app.yaml",
                           "Application:\n"
                           "  RuntimeScript: ./runtime.js\n"));
    AppManifest manifest;
    std::string error;
    XER_VE_CHECK(apprt::LoadAppManifest(dir, &manifest, &error));
    XER_VE_CHECK(manifest.runtime_script == "./runtime.js");
    XER_VE_CHECK(!manifest.base_dir.empty());
}

// Application present but no RuntimeScript key -> parse fails.
void TestManifestMissingScriptFails() {
    const auto dir = TempRoot() / "noscript";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(dir / "app.yaml",
                           "Application:\n"
                           "  Name: x\n"));
    AppManifest manifest;
    std::string error;
    XER_VE_CHECK(!apprt::LoadAppManifest(dir, &manifest, &error));
}

// window.yaml with only a Title keeps the compiled-in geometry
// defaults (1440x900).
void TestWindowConfigDefaults() {
    const auto dir = TempRoot() / "win_defaults";
    std::filesystem::create_directories(dir);
    const auto path = dir / "window.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Window:\n"
                           "  Title: \"T\"\n"));
    WindowConfig config;
    std::string error;
    XER_VE_CHECK(apprt::LoadWindowConfig(path, &config, &error));
    XER_VE_CHECK(config.title == "T");
    XER_VE_CHECK(config.width == 1440);
    XER_VE_CHECK(config.height == 900);
}

// The real window.yaml shape: Size {w,h}, Title, and the
// boolean-ish OpenDevTools string.
void TestWindowConfigSizeAndBools() {
    const auto dir = TempRoot() / "win_full";
    std::filesystem::create_directories(dir);
    const auto path = dir / "window.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Window:\n"
                           "  Size: {w: 1560, h: 900}\n"
                           "  Title: \"Video Editor\"\n"
                           "  OpenDevTools: \"true\"\n"));
    WindowConfig config;
    std::string error;
    XER_VE_CHECK(apprt::LoadWindowConfig(path, &config, &error));
    XER_VE_CHECK(config.width == 1560);
    XER_VE_CHECK(config.height == 900);
    XER_VE_CHECK(config.title == "Video Editor");
    XER_VE_CHECK(config.open_devtools);
}

// %WEAKLY_ROOT_PATH_EXECUTE% always resolves to the exe directory —
// the expansion must contain no '%' and carry a path separator.
void TestExpandEnvWeaklyRoot() {
    const std::string expanded =
        apprt::ExpandEnvPath("%WEAKLY_ROOT_PATH_EXECUTE%/assets");
    XER_VE_CHECK(!expanded.empty());
    XER_VE_CHECK(expanded.find('%') == std::string::npos);
    XER_VE_CHECK(expanded.find('/') != std::string::npos ||
                 expanded.find('\\') != std::string::npos);
}

// Engine lifecycle: Create() yields a usable isolate/context, valid
// source runs, and a syntax error is reported with non-empty text.
void TestEngineCreateAndRun() {
    JsEngine* engine = SharedEngine();
    if (engine == nullptr) {
        XER_VE_CHECK(engine != nullptr);  // counts the failure
        return;
    }
    std::string error;
    XER_VE_CHECK(engine->RunSource("exports.x = 1;", "t.js", &error));
    XER_VE_CHECK(!engine->RunSource("syntax error(", "bad.js", &error));
    XER_VE_CHECK(!error.empty());
}

// app_super_exportAppData.setOutAppData() stores flat key->value
// pairs into the engine's out-data registry; the real runtime.js
// flow keys the window-yaml path off m.ENUM.CONFIG_WINDOW.
void TestEngineOutDataViaScript() {
    JsEngine* engine = SharedEngine();
    if (engine == nullptr) {
        XER_VE_CHECK(engine != nullptr);
        return;
    }
    apprt::RegisterAll(*engine);
    std::string error;
    XER_VE_CHECK(engine->RunSource(
        "const m = NativeImport(\"app_super_exportAppData\");\n"
        "m.setOutAppData({ \"k\": \"v\" });\n",
        "outdata.js", &error));
    const OutValue v = engine->GetOutData("k");
    XER_VE_CHECK(v.kind == OutValue::Kind::String);
    XER_VE_CHECK(v.str == "v");

    // Mirror runtime.js: computed key via ENUM.CONFIG_WINDOW.
    XER_VE_CHECK(engine->RunSource(
        "const m2 = NativeImport(\"app_super_exportAppData\");\n"
        "m2.setOutAppData({ [m2.ENUM.CONFIG_WINDOW]: "
        "\"C:/x/window.yaml\" });\n",
        "outdata_enum.js", &error));
    const OutValue w = engine->GetOutData(apprt::kConfigWindow);
    XER_VE_CHECK(w.kind == OutValue::Kind::String);
    XER_VE_CHECK(w.str == "C:/x/window.yaml");
}

// app_super_concat.handle({join:"/"}, a, b, ...) joins args with the
// separator — result is ferried back through setOutAppData.
void TestEngineNativeImportModules() {
    JsEngine* engine = SharedEngine();
    if (engine == nullptr) {
        XER_VE_CHECK(engine != nullptr);
        return;
    }
    apprt::RegisterAll(*engine);
    std::string error;
    // Note: top-level consts persist in the shared context across
    // scripts — unique names avoid a re-declaration SyntaxError.
    XER_VE_CHECK(engine->RunSource(
        "const mx = NativeImport(\"app_super_exportAppData\");\n"
        "const c = NativeImport(\"app_super_concat\");\n"
        "mx.setOutAppData({ \"joined\": c.handle({join: \"/\"}, "
        "\"a\", \"b\") });\n",
        "concat.js", &error));
    const OutValue joined = engine->GetOutData("joined");
    XER_VE_CHECK(joined.kind == OutValue::Kind::String);
    XER_VE_CHECK(joined.str == "a/b");
}

// Boot() with no <exe>/assets and no CWD/assets must degrade: ok ==
// false, no engine, and the WindowConfig defaults still populated.
void TestBootMissingAssets() {
    // The test binary lives in bin/ next to the deployed assets/ — the
    // exe-dir check hits even under a foreign CWD, so this exercises the
    // full chain: app.yaml → runtime.js (real V8) → window.yaml.
    const auto dir = TempRoot() / "boot_empty";
    std::filesystem::create_directories(dir);
    std::string status;
    BootResult result;
    {
        CwdGuard cwd;
        std::error_code ec;
        std::filesystem::current_path(dir, ec);
        XER_VE_CHECK(!ec);
        result = apprt::Boot(&status);
    }
    XER_VE_CHECK(result.ok);
    XER_VE_CHECK(result.engine != nullptr);
    XER_VE_CHECK(result.window.title == "XER Studio");
    XER_VE_CHECK(result.window.width == 1560);
    XER_VE_CHECK(result.window.height == 900);
}

// FirePluginLoad on a null engine is a no-op — must not crash.
void TestFirePluginLoadNoEngine() {
    apprt::FirePluginLoad(nullptr, "p", "n", "v", "f");
}

// Shared test icon: a filled 4..20 rect in a 24x24 viewBox — the
// center pixel is solid, the corner is empty.
const char kTestSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
    "<path d=\"M4,4 H20 V20 H4 Z\" fill=\"#fff\"/></svg>";

// A filled rect rasterizes to a solid interior and a transparent
// exterior: BGRA size matches out_size^2*4, the tinted B channel is
// set, alpha is solid inside the path and 0 outside it.
void TestSvgRasterBasic() {
    std::vector<std::uint8_t> bgra;
    XER_VE_CHECK(apprt::RasterizeSvgIcon(kTestSvg, 24, 0xFF0000FF, &bgra));
    XER_VE_CHECK(bgra.size() == 24u * 24 * 4);
    if (bgra.size() == 24u * 24 * 4) {
        const size_t center = (12 * 24 + 12) * 4;
        XER_VE_CHECK(bgra[center] > 0);        // B channel (blue tint)
        XER_VE_CHECK(bgra[center + 3] > 200);  // solid fill alpha
        XER_VE_CHECK(bgra[3] == 0);  // (0,0) outside the 4..20 rect
    }
}

// Non-SVG input must fail cleanly with false.
void TestSvgRasterInvalid() {
    std::vector<std::uint8_t> bgra;
    XER_VE_CHECK(
        !apprt::RasterizeSvgIcon("not svg", 24, 0xFFFFFFFF, &bgra));
}

// icons.yaml: the Icons: map loads name→file pairs verbatim; a
// missing file returns false.
void TestIconsManifest() {
    const auto dir = TempRoot() / "icons_yaml";
    std::filesystem::create_directories(dir);
    const auto path = dir / "icons.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Icons:\n"
                           "  nav.media: apps.svg\n"
                           "  t.p: x.svg\n"));
    std::unordered_map<std::string, std::string> map;
    std::string error;
    XER_VE_CHECK(apprt::LoadIconsManifest(path, &map, &error));
    XER_VE_CHECK(map.size() == 2);
    XER_VE_CHECK(map["nav.media"] == "apps.svg");

    XER_VE_CHECK(
        !apprt::LoadIconsManifest(dir / "missing.yaml", &map, &error));
}

// The documented editor.yaml schema: every field round-trips into
// the EditorConfig struct.
void TestEditorConfigFull() {
    const auto dir = TempRoot() / "editor_full";
    std::filesystem::create_directories(dir);
    const auto path = dir / "editor.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Editor:\n"
                           "  Theme: dark\n"
                           "  Accent: \"#8B5CF6\"\n"
                           "  RightTab: 2\n"
                           "  Panels:\n"
                           "    Left: false\n"
                           "    Right: true\n"
                           "  Timeline:\n"
                           "    Height: 300\n"
                           "  MediaBin:\n"
                           "    View: list\n"
                           "  StatusBar: false\n"));
    EditorConfig config;
    std::string error;
    XER_VE_CHECK(apprt::LoadEditorConfig(path, &config, &error));
    XER_VE_CHECK(config.theme == "dark");
    XER_VE_CHECK(config.accent_argb == 0xFF8B5CF6u);
    XER_VE_CHECK(config.right_tab == 2);
    XER_VE_CHECK(!config.left_panel);
    XER_VE_CHECK(config.timeline_height == 300);
    XER_VE_CHECK(config.media_bin_view == "list");
    XER_VE_CHECK(!config.show_status_bar);
}

// A nonexistent editor.yaml path must return false.
void TestEditorConfigMissing() {
    EditorConfig config;
    std::string error;
    XER_VE_CHECK(!apprt::LoadEditorConfig(
        TempRoot() / "no_such_editor.yaml", &config, &error));
}

// shortcuts.yaml: the Shortcuts: map loads action→combo pairs.
void TestShortcutsConfig() {
    const auto dir = TempRoot() / "shortcuts";
    std::filesystem::create_directories(dir);
    const auto path = dir / "shortcuts.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Shortcuts:\n"
                           "  playback.toggle: Space\n"));
    ShortcutsConfig config;
    std::string error;
    XER_VE_CHECK(apprt::LoadShortcutsConfig(path, &config, &error));
    XER_VE_CHECK(config.keys["playback.toggle"] == "Space");
}

// IconPack rasterizes a mapped logical name from its icons dir;
// unmapped names fall back to "<name>.svg" and miss cleanly.
void TestIconPackSvg() {
    const auto dir = TempRoot() / "iconpack" / "icons";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(dir / "plus.svg", kTestSvg));
    IconPack pack;
    pack.Configure(dir, {{"act.plus", "plus.svg"}});
    const IconBitmap* icon = pack.Get("act.plus", 24, 0xFFFF0000);
    XER_VE_CHECK(icon != nullptr);
    if (icon != nullptr) {
        XER_VE_CHECK(icon->ok);
        XER_VE_CHECK(icon->width == 24);
    }
    XER_VE_CHECK(pack.Get("missing", 24, 0xFFFF0000) == nullptr);
    XER_VE_CHECK(pack.Has("act.plus"));
}

// Boot() on the real deployed assets/ (the test exe sits next to it)
// must populate SharedAssetsConfig + SharedIconPack from editor.yaml
// and icons.yaml. Boot is idempotent — the earlier boot case may
// already have run it, so call it fresh here.
void TestBootAssetsRegistry() {
    std::string status;
    BootResult result = apprt::Boot(&status);
    XER_VE_CHECK(result.ok);
    const auto& assets = apprt::SharedAssetsConfig();
    XER_VE_CHECK(assets.editor.theme == "dark");
    XER_VE_CHECK(assets.editor.accent_argb == 0xFF8B5CF6u);
    XER_VE_CHECK(!assets.icon_map.empty());
    XER_VE_CHECK(apprt::SharedIconPack().Get("nav.media", 20,
                                             0xFFFFFFFF) != nullptr);
}

// toolbar.json: the Toolbar.buttons array loads id/label/icon/action;
// "type":"separator" entries set the separator flag and carry no
// action. Malformed JSON must fail cleanly with false.
void TestToolbarJson() {
    const auto dir = TempRoot() / "toolbar";
    std::filesystem::create_directories(dir);
    const auto path = dir / "toolbar.json";
    XER_VE_CHECK(WriteFile(
        path,
        "{\"Toolbar\":{\"buttons\":["
        "{\"id\":\"import\",\"label\":\"Import\",\"icon\":\"bin.import\","
        "\"action\":\"file.import\"},"
        "{\"id\":\"sep\",\"type\":\"separator\"},"
        "{\"id\":\"undo\",\"label\":\"Undo\",\"action\":\"edit.undo\"}"
        "]}}"));
    std::vector<apprt::ToolbarButton> buttons;
    std::string error;
    XER_VE_CHECK(apprt::LoadToolbarJson(path, &buttons, &error));
    XER_VE_CHECK(buttons.size() == 3);
    if (buttons.size() == 3) {
        XER_VE_CHECK(buttons[0].action == "file.import");
        XER_VE_CHECK(!buttons[0].separator);
        XER_VE_CHECK(buttons[1].separator);
        XER_VE_CHECK(buttons[2].action == "edit.undo");
    }

    XER_VE_CHECK(WriteFile(dir / "bad.json", "{ this is not json"));
    XER_VE_CHECK(
        !apprt::LoadToolbarJson(dir / "bad.json", &buttons, &error));
}

// strings.ini: [labels] key=value pairs load into UiStrings — Get()
// returns the stored label or the caller fallback for unknown keys.
void TestStringsIni() {
    const auto dir = TempRoot() / "strings";
    std::filesystem::create_directories(dir);
    const auto path = dir / "strings.ini";
    XER_VE_CHECK(WriteFile(path,
                           "[labels]\n"
                           "app_name = XER Studio\n"
                           "; comment\n"));
    apprt::UiStrings strings;
    std::string error;
    XER_VE_CHECK(apprt::LoadStringsIni(path, &strings, &error));
    XER_VE_CHECK(strings.Get("app_name", "x") == "XER Studio");
    XER_VE_CHECK(strings.Get("missing", "fb") == "fb");
}

// theme.toml: [colors] hex values parse to ARGB, [metrics] numbers to
// doubles.
void TestThemeToml() {
    const auto dir = TempRoot() / "theme";
    std::filesystem::create_directories(dir);
    const auto path = dir / "theme.toml";
    XER_VE_CHECK(WriteFile(path,
                           "[colors]\n"
                           "accent = \"#8B5CF6\"\n"
                           "[metrics]\n"
                           "radius = 8\n"));
    apprt::ThemeOverride theme;
    std::string error;
    XER_VE_CHECK(apprt::LoadThemeToml(path, &theme, &error));
    XER_VE_CHECK(theme.colors["accent"] == 0xFF8B5CF6u);
    XER_VE_CHECK(theme.metrics["radius"] == 8.0);
}

// layout.yaml: NavRail.items is an ordered string list; Panels.right
// maps visible/width/tab onto the LayoutConfig fields.
void TestLayoutYaml() {
    const auto dir = TempRoot() / "layout";
    std::filesystem::create_directories(dir);
    const auto path = dir / "layout.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Layout:\n"
                           "  NavRail:\n"
                           "    items: [media, fx]\n"
                           "  Panels:\n"
                           "    right: {visible: false, width: 400, tab: 3}\n"));
    apprt::LayoutConfig layout;
    std::string error;
    XER_VE_CHECK(apprt::LoadLayoutYaml(path, &layout, &error));
    XER_VE_CHECK(layout.nav_items.size() == 2);
    if (layout.nav_items.size() == 2) {
        XER_VE_CHECK(layout.nav_items[0] == "media");
        XER_VE_CHECK(layout.nav_items[1] == "fx");
    }
    XER_VE_CHECK(!layout.right_visible);
    XER_VE_CHECK(layout.right_width == 400);
    XER_VE_CHECK(layout.right_tab == 3);
}

// menus.yaml: the Menus: sequence loads ordered menus of ordered
// items; {separator: true} rows set the separator flag and
// {danger: true} marks destructive entries.
void TestMenusYamlFull() {
    const auto dir = TempRoot() / "menus";
    std::filesystem::create_directories(dir);
    const auto path = dir / "menus.yaml";
    XER_VE_CHECK(WriteFile(
        path,
        "Menus:\n"
        "  - id: media_bin.item\n"
        "    items:\n"
        "      - {action: bin.add_to_timeline, label: \"Add to Timeline\","
        " icon: bin.add, shortcut: \"Enter\"}\n"
        "      - {separator: true}\n"
        "      - {action: bin.delete, label: \"Delete Clip\","
        " danger: true}\n"
        "  - id: monitor\n"
        "    items:\n"
        "      - {action: mon.loop, label: \"Loop\"}\n"));
    std::vector<apprt::MenuDef> menus;
    std::string error;
    XER_VE_CHECK(apprt::LoadMenusYaml(path, &menus, &error));
    XER_VE_CHECK(menus.size() == 2);
    if (menus.size() == 2) {
        XER_VE_CHECK(menus[0].id == "media_bin.item");
        XER_VE_CHECK(menus[0].items.size() == 3);
        if (menus[0].items.size() == 3) {
            const auto& first = menus[0].items[0];
            XER_VE_CHECK(first.action == "bin.add_to_timeline");
            XER_VE_CHECK(first.label == "Add to Timeline");
            XER_VE_CHECK(first.icon == "bin.add");
            XER_VE_CHECK(first.shortcut == "Enter");
            XER_VE_CHECK(!first.separator);
            XER_VE_CHECK(!first.danger);
            XER_VE_CHECK(menus[0].items[1].separator);
            XER_VE_CHECK(menus[0].items[2].danger);
            XER_VE_CHECK(menus[0].items[2].action == "bin.delete");
        }
        XER_VE_CHECK(menus[1].id == "monitor");
        XER_VE_CHECK(menus[1].items.size() == 1);
    }
}

// A nonexistent menus.yaml path must return false with a non-empty
// error. (Like LoadToolbarJson, the loader leaves `out` untouched on
// failure, so only the return value and error are asserted.)
void TestMenusYamlMissing() {
    std::vector<apprt::MenuDef> menus;
    std::string error;
    XER_VE_CHECK(!apprt::LoadMenusYaml(TempRoot() / "no_such_menus.yaml",
                                     &menus, &error));
    XER_VE_CHECK(!error.empty());
}

// Malformed YAML (an unclosed flow map) must fail cleanly with false
// rather than throwing through the loader.
void TestMenusYamlMalformed() {
    const auto dir = TempRoot() / "menus_bad";
    std::filesystem::create_directories(dir);
    const auto path = dir / "menus.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Menus:\n"
                           "  - id: unclosed\n"
                           "    items: [{action: x\n"));
    std::vector<apprt::MenuDef> menus;
    std::string error;
    XER_VE_CHECK(!apprt::LoadMenusYaml(path, &menus, &error));
    XER_VE_CHECK(!error.empty());
}

// effects.yaml: the Effects: sequence loads id/label/category/icon and
// a params list where kind selects which fields matter — sliders carry
// min/max/default/unit, choices carry a string list.
void TestEffectsYamlFull() {
    const auto dir = TempRoot() / "effects";
    std::filesystem::create_directories(dir);
    const auto path = dir / "effects.yaml";
    XER_VE_CHECK(WriteFile(
        path,
        "Effects:\n"
        "  - id: blur.gaussian\n"
        "    label: Gaussian Blur\n"
        "    category: Blur\n"
        "    icon: fx.blur\n"
        "    params:\n"
        "      - {id: radius, label: Radius, kind: slider, min: 0,"
        " max: 100, default: 8, unit: px}\n"
        "      - {id: channel, label: Channel, kind: choice,"
        " choices: [luma, rgb]}\n"
        "  - id: color.invert\n"
        "    label: Invert\n"
        "    category: Color\n"));
    std::vector<apprt::EffectDef> effects;
    std::string error;
    XER_VE_CHECK(apprt::LoadEffectsYaml(path, &effects, &error));
    XER_VE_CHECK(effects.size() == 2);
    if (effects.size() == 2) {
        XER_VE_CHECK(effects[0].id == "blur.gaussian");
        XER_VE_CHECK(effects[0].label == "Gaussian Blur");
        XER_VE_CHECK(effects[0].category == "Blur");
        XER_VE_CHECK(effects[0].icon == "fx.blur");
        XER_VE_CHECK(effects[0].params.size() == 2);
        if (effects[0].params.size() == 2) {
            const auto& slider = effects[0].params[0];
            XER_VE_CHECK(slider.id == "radius");
            XER_VE_CHECK(slider.kind == "slider");
            XER_VE_CHECK(slider.min_v == 0.0);
            XER_VE_CHECK(slider.max_v == 100.0);
            XER_VE_CHECK(slider.default_v == 8.0);
            XER_VE_CHECK(slider.unit == "px");
            const auto& choice = effects[0].params[1];
            XER_VE_CHECK(choice.kind == "choice");
            XER_VE_CHECK(choice.choices.size() == 2);
            if (choice.choices.size() == 2) {
                XER_VE_CHECK(choice.choices[0] == "luma");
                XER_VE_CHECK(choice.choices[1] == "rgb");
            }
        }
        XER_VE_CHECK(effects[1].id == "color.invert");
        XER_VE_CHECK(effects[1].params.empty());
    }
}

// inspector.yaml: the Inspector: sequence maps a selection kind to an
// ordered list of sections, each with ordered fields.
void TestInspectorYamlFull() {
    const auto dir = TempRoot() / "inspector";
    std::filesystem::create_directories(dir);
    const auto path = dir / "inspector.yaml";
    XER_VE_CHECK(WriteFile(
        path,
        "Inspector:\n"
        "  - selection: clip.video\n"
        "    sections:\n"
        "      - {id: transform, title: Transform, fields:"
        " [{id: position, kind: vec2}, {id: scale, kind: vec2}]}\n"
        "      - {id: opacity, title: Opacity, fields:"
        " [{id: opacity, kind: slider}]}\n"));
    std::vector<apprt::InspectorSchemaDef> schemas;
    std::string error;
    XER_VE_CHECK(apprt::LoadInspectorYaml(path, &schemas, &error));
    XER_VE_CHECK(schemas.size() == 1);
    if (schemas.size() == 1) {
        XER_VE_CHECK(schemas[0].selection == "clip.video");
        XER_VE_CHECK(schemas[0].sections.size() == 2);
        if (schemas[0].sections.size() == 2) {
            const auto& transform = schemas[0].sections[0];
            XER_VE_CHECK(transform.id == "transform");
            XER_VE_CHECK(transform.title == "Transform");
            XER_VE_CHECK(transform.fields.size() == 2);
            if (transform.fields.size() == 2) {
                XER_VE_CHECK(transform.fields[0].id == "position");
                XER_VE_CHECK(transform.fields[0].kind == "vec2");
                XER_VE_CHECK(transform.fields[1].id == "scale");
            }
            const auto& opacity = schemas[0].sections[1];
            XER_VE_CHECK(opacity.id == "opacity");
            XER_VE_CHECK(opacity.fields.size() == 1);
            if (opacity.fields.size() == 1) {
                XER_VE_CHECK(opacity.fields[0].kind == "slider");
            }
        }
    }
}

// workspaces.yaml: the Workspaces: sequence mirrors Ui::WorkspaceLayout
// — name, panel visibility, right tab index, and timeline fraction.
void TestWorkspacesYamlFull() {
    const auto dir = TempRoot() / "workspaces";
    std::filesystem::create_directories(dir);
    const auto path = dir / "workspaces.yaml";
    XER_VE_CHECK(WriteFile(
        path,
        "Workspaces:\n"
        "  - {name: Editing, left_visible: true, right_visible: true,"
        " right_tab: 0, timeline_fraction: 0.32}\n"
        "  - {name: Color, left_visible: false, right_visible: true,"
        " right_tab: 2, timeline_fraction: 0.5}\n"
        "  - {name: Audio, left_visible: true, right_visible: false,"
        " right_tab: 1, timeline_fraction: 0.6}\n"));
    std::vector<apprt::WorkspaceDef> workspaces;
    std::string error;
    XER_VE_CHECK(apprt::LoadWorkspacesYaml(path, &workspaces, &error));
    XER_VE_CHECK(workspaces.size() == 3);
    if (workspaces.size() == 3) {
        XER_VE_CHECK(workspaces[0].name == "Editing");
        XER_VE_CHECK(workspaces[0].left_visible);
        XER_VE_CHECK(workspaces[0].right_visible);
        XER_VE_CHECK(workspaces[0].right_tab == 0);
        XER_VE_CHECK(workspaces[0].timeline_fraction == 0.32);
        XER_VE_CHECK(workspaces[1].name == "Color");
        XER_VE_CHECK(!workspaces[1].left_visible);
        XER_VE_CHECK(workspaces[1].right_tab == 2);
        XER_VE_CHECK(workspaces[1].timeline_fraction == 0.5);
        XER_VE_CHECK(workspaces[2].name == "Audio");
        XER_VE_CHECK(!workspaces[2].right_visible);
        XER_VE_CHECK(workspaces[2].right_tab == 1);
    }
}

// monitor.yaml: Transport.buttons is an ordered string list and
// Overlays carries the safe-margins/grid bools plus the default
// playback quality string.
void TestMonitorYamlFull() {
    const auto dir = TempRoot() / "monitor";
    std::filesystem::create_directories(dir);
    const auto path = dir / "monitor.yaml";
    XER_VE_CHECK(WriteFile(
        path,
        "Monitor:\n"
        "  Transport:\n"
        "    buttons: [play, frame_back, frame_fwd, loop, fullscreen]\n"
        "  Overlays:\n"
        "    safe_margins: true\n"
        "    grid: true\n"
        "    default_quality: half\n"));
    apprt::MonitorConfig config;
    std::string error;
    XER_VE_CHECK(apprt::LoadMonitorYaml(path, &config, &error));
    XER_VE_CHECK(config.transport_buttons.size() == 5);
    if (config.transport_buttons.size() == 5) {
        XER_VE_CHECK(config.transport_buttons[0] == "play");
        XER_VE_CHECK(config.transport_buttons[4] == "fullscreen");
    }
    XER_VE_CHECK(config.safe_margins);
    XER_VE_CHECK(config.grid);
    XER_VE_CHECK(config.default_quality == "half");
}

// panels.yaml: dock values are validated against the known set (an
// unknown dock falls back to "right" and earns a note in `error` on a
// true return), sizes clamp to the unit range, and a negative
// min_size clamps to 0.
void TestPanelsYaml() {
    const auto dir = TempRoot() / "panels";
    std::filesystem::create_directories(dir);
    const auto path = dir / "panels.yaml";
    XER_VE_CHECK(WriteFile(
        path,
        "Panels:\n"
        "  dock_size_unit: fraction\n"
        "  splitter_thickness: 8\n"
        "  workspace_order: [Editing, Color]\n"
        "  panels:\n"
        "    - {id: media_bin, dock: left, size: 0.25, min_size: 0.1}\n"
        "    - {id: inspector, dock: right, size: 0.3, min_size: 0.15,\n"
        "       tabs: [inspector, effects]}\n"
        "    - {id: stray, dock: diagonal, size: 2.5, min_size: -4}\n"));
    apprt::PanelsConfig config;
    std::string error;
    XER_VE_CHECK(apprt::LoadPanelsConfig(path, &config, &error));
    XER_VE_CHECK(config.dock_size_unit == "fraction");
    XER_VE_CHECK(config.splitter_thickness == 8);
    XER_VE_CHECK(config.workspace_order.size() == 2);
    XER_VE_CHECK(config.panels.size() == 3);
    if (config.panels.size() == 3) {
        XER_VE_CHECK(config.panels[0].id == "media_bin");
        XER_VE_CHECK(config.panels[0].dock == "left");
        XER_VE_CHECK(config.panels[0].size == 0.25f);
        XER_VE_CHECK(config.panels[0].visible);
        XER_VE_CHECK(config.panels[1].dock == "right");
        XER_VE_CHECK(config.panels[1].tabs.size() == 2);
        // Unknown dock -> "right" + note; out-of-range values clamp.
        XER_VE_CHECK(config.panels[2].dock == "right");
        XER_VE_CHECK(config.panels[2].size == 1.0f);
        XER_VE_CHECK(config.panels[2].min_size == 0);
    }
    // The unknown-dock note travels in `error` on a true return.
    XER_VE_CHECK(error.find("diagonal") != std::string::npos);

    // px unit: sizes clamp to the px bound, not the 0..1 band.
    const auto px_path = dir / "panels_px.yaml";
    XER_VE_CHECK(WriteFile(px_path,
                           "Panels:\n"
                           "  dock_size_unit: px\n"
                           "  panels:\n"
                           "    - {id: wide, dock: bottom, size: 99999,"
                           " min_size: 240}\n"));
    config = apprt::PanelsConfig{};
    error.clear();
    XER_VE_CHECK(apprt::LoadPanelsConfig(px_path, &config, &error));
    XER_VE_CHECK(config.panels.size() == 1);
    if (config.panels.size() == 1) {
        XER_VE_CHECK(config.panels[0].size == 4000.0f);
        XER_VE_CHECK(config.panels[0].min_size == 240);
    }

    // Fraction unit + px min_size: the contradiction clamp must NOT
    // compare a 0..1 size against a px bound — (int)0.3 would collapse
    // the min to 0. The resizer resolves the px min at layout.
    const auto frac_path = dir / "panels_frac_min.yaml";
    XER_VE_CHECK(WriteFile(frac_path,
                           "Panels:\n"
                           "  dock_size_unit: fraction\n"
                           "  panels:\n"
                           "    - {id: side, dock: left, size: 0.3,"
                           " min_size: 250}\n"));
    config = apprt::PanelsConfig{};
    error.clear();
    XER_VE_CHECK(apprt::LoadPanelsConfig(frac_path, &config, &error));
    XER_VE_CHECK(config.panels.size() == 1);
    if (config.panels.size() == 1) {
        XER_VE_CHECK(config.panels[0].size == 0.3f);
        XER_VE_CHECK(config.panels[0].min_size == 250);
    }

    // Missing Panels: section is a hard failure, like the other loaders.
    const auto bare = dir / "panels_bare.yaml";
    XER_VE_CHECK(WriteFile(bare, "Other:\n  x: 1\n"));
    XER_VE_CHECK(!apprt::LoadPanelsConfig(bare, &config, &error));

    XER_VE_CHECK(!apprt::LoadPanelsConfig(dir / "missing.yaml", &config,
                                        &error));
}

// workspaces.yaml (panel view): each entry maps a name to per-panel
// visibility/size overrides plus the tab to raise; column-level fields
// from the WorkspaceDef schema coexist in the same entry.
void TestWorkspacesConfigYaml() {
    const auto dir = TempRoot() / "workspaces_cfg";
    std::filesystem::create_directories(dir);
    const auto path = dir / "workspaces.yaml";
    XER_VE_CHECK(WriteFile(
        path,
        "Workspaces:\n"
        "  - name: Editing\n"
        "    active_tab: inspector\n"
        "    panel_visibility: {media_bin: true, mixer: false}\n"
        "    panel_sizes: {media_bin: 320, timeline: 240}\n"
        "  - name: Audio\n"
        "    active_tab: mixer\n"
        "    panel_visibility: {media_bin: false, mixer: true}\n"
        "    panel_sizes: {mixer: 400}\n"));
    std::vector<apprt::WorkspaceSpec> workspaces;
    std::string error;
    XER_VE_CHECK(
        apprt::LoadWorkspacesConfig(path, &workspaces, &error));
    XER_VE_CHECK(workspaces.size() == 2);
    if (workspaces.size() == 2) {
        XER_VE_CHECK(workspaces[0].name == "Editing");
        XER_VE_CHECK(workspaces[0].active_tab == "inspector");
        XER_VE_CHECK(workspaces[0].panel_visibility.size() == 2);
        XER_VE_CHECK(workspaces[0].panel_visibility.count("media_bin") ==
                     1);
        XER_VE_CHECK(workspaces[0].panel_visibility.at("media_bin"));
        XER_VE_CHECK(!workspaces[0].panel_visibility.at("mixer"));
        XER_VE_CHECK(workspaces[0].panel_sizes.at("timeline") == 240.0f);
        XER_VE_CHECK(workspaces[1].name == "Audio");
        XER_VE_CHECK(workspaces[1].active_tab == "mixer");
        XER_VE_CHECK(!workspaces[1].panel_visibility.at("media_bin"));
        XER_VE_CHECK(workspaces[1].panel_sizes.at("mixer") == 400.0f);
    }

    XER_VE_CHECK(!apprt::LoadWorkspacesConfig(dir / "missing.yaml",
                                            &workspaces, &error));
}

// The shared stores behind FindMenuDef/FindEffectDef: loading a file
// straight into SharedMenusConfig()/SharedEffectsCatalog() makes the
// entries findable by id, and an absent id returns nullptr. Unique
// "ut_shared.*" ids avoid colliding with anything Boot() may already
// have loaded; the stores are restored afterwards so later cases see
// no leftovers.
void TestSharedFinders() {
    const auto dir = TempRoot() / "shared_finders";
    std::filesystem::create_directories(dir);
    const auto menus_path = dir / "menus.yaml";
    const auto effects_path = dir / "effects.yaml";
    XER_VE_CHECK(WriteFile(menus_path,
                           "Menus:\n"
                           "  - id: ut_shared.menu\n"
                           "    items:\n"
                           "      - {action: ut.act, label: \"UT\"}\n"));
    XER_VE_CHECK(WriteFile(effects_path,
                           "Effects:\n"
                           "  - id: ut_shared.effect\n"
                           "    label: UT Effect\n"));

    auto& menus = apprt::SharedMenusConfig();
    auto& effects = apprt::SharedEffectsCatalog();
    const size_t menus_before = menus.size();
    const size_t effects_before = effects.size();

    std::string error;
    XER_VE_CHECK(apprt::LoadMenusYaml(menus_path, &menus, &error));
    XER_VE_CHECK(apprt::LoadEffectsYaml(effects_path, &effects, &error));

    const apprt::MenuDef* menu = apprt::FindMenuDef("ut_shared.menu");
    XER_VE_CHECK(menu != nullptr);
    if (menu != nullptr) {
        XER_VE_CHECK(menu->id == "ut_shared.menu");
        XER_VE_CHECK(menu->items.size() == 1);
    }
    XER_VE_CHECK(apprt::FindMenuDef("ut_shared.absent") == nullptr);

    const apprt::EffectDef* effect =
        apprt::FindEffectDef("ut_shared.effect");
    XER_VE_CHECK(effect != nullptr);
    if (effect != nullptr) {
        XER_VE_CHECK(effect->label == "UT Effect");
    }
    XER_VE_CHECK(apprt::FindEffectDef("ut_shared.absent") == nullptr);

    menus.resize(menus_before);
    effects.resize(effects_before);
}

// color.yaml: the Color: map loads Source/Display/Enabled into the
// OcioConfig struct.
void TestOcioConfig() {
    const auto dir = TempRoot() / "color";
    std::filesystem::create_directories(dir);
    const auto path = dir / "color.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Color:\n"
                           "  Source: \"sRGB\"\n"
                           "  Display: \"sRGB\"\n"
                           "  Enabled: true\n"));
    vecolor::OcioConfig config;
    std::string error;
    XER_VE_CHECK(vecolor::LoadOcioConfig(path, &config, &error));
    XER_VE_CHECK(config.source == "sRGB");
    XER_VE_CHECK(config.display == "sRGB");
    XER_VE_CHECK(config.enabled);
}

// OcioAvailable and ApplyDisplayTransform share the same cached
// processor build — they must agree. (The builtin raw config may carry
// no displays, in which case both correctly report false.)
void TestOcioTransform() {
    std::vector<std::uint8_t> bgra(4 * 4 * 4, 128);
    for (size_t i = 3; i < bgra.size(); i += 4) {
        bgra[i] = 255;  // opaque alpha
    }
    const vecolor::OcioConfig config;  // defaults -> OCIO builtin config
    const bool available = vecolor::OcioAvailable(config);
    const bool applied = vecolor::ApplyDisplayTransform(
        bgra.data(), 4, 4, 4 * 4, config);
    XER_VE_CHECK(applied == available);
    XER_VE_CHECK(bgra[3] == 255);  // alpha untouched either way
}

// OIIO round-trip: a 4x4 BGRA checkerboard written as PNG reads back
// with the same dimensions and the corner pixel within 2 LSB.
void TestOiioRoundtrip() {
    const auto path = TempRoot() / "t.png";
    constexpr std::uint8_t kA[4] = {0x20, 0x40, 0x80, 0xFF};  // B,G,R,A
    constexpr std::uint8_t kB[4] = {0xC0, 0xA0, 0x40, 0xFF};
    std::vector<std::uint8_t> bgra(4 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::uint8_t* c = ((x + y) & 1) ? kB : kA;
            std::copy_n(c, 4, bgra.data() + (y * 4 + x) * 4);
        }
    }
    XER_VE_CHECK(veio::WriteImage(path, bgra.data(), 4, 4));

    const veio::Image img = veio::ReadImage(path);
    XER_VE_CHECK(img.ok);
    if (img.ok) {
        XER_VE_CHECK(img.width == 4);
        XER_VE_CHECK(img.height == 4);
        XER_VE_CHECK(img.bgra.size() >= 4);
        if (img.bgra.size() >= 4) {
            // (0,0) is color A — PNG is lossless but allow +/-2 LSB
            // for any conversion rounding.
            for (int ch = 0; ch < 4; ++ch) {
                const int d =
                    static_cast<int>(img.bgra[ch]) - static_cast<int>(kA[ch]);
                XER_VE_CHECK(d >= -2 && d <= 2);
            }
        }
    }
}

// OiioHandlesExtension covers formats WIC can't (exr, dpx) and leaves
// the ones WIC already handles (png) alone.
void TestOiioHandles() {
    XER_VE_CHECK(veio::OiioHandlesExtension("exr"));
    XER_VE_CHECK(!veio::OiioHandlesExtension("png"));
    XER_VE_CHECK(veio::OiioHandlesExtension("dpx"));
}

// Boot() on the deployed assets/ also fills the UI-manifest shared
// singletons: strings.ini -> SharedUiStrings, layout.yaml ->
// SharedLayoutConfig, theme.toml -> SharedThemeOverride, and
// toolbar.json action ids -> SharedAssetsConfig().toolbar_ids.
void TestUiManifestShared() {
    std::string status;
    BootResult result = apprt::Boot(&status);
    XER_VE_CHECK(result.ok);
    XER_VE_CHECK(apprt::SharedUiStrings().Get("app_name", "x") ==
                 "XER Studio");
    XER_VE_CHECK(apprt::SharedLayoutConfig().right_width == 340);
    XER_VE_CHECK(apprt::SharedThemeOverride().colors["accent"] ==
                 0xFF8B5CF6u);
    const auto& ids = apprt::SharedAssetsConfig().toolbar_ids;
    XER_VE_CHECK(std::find(ids.begin(), ids.end(), "file.import") !=
                 ids.end());
    // panels.yaml / workspaces.yaml ride the same out-data keys — Boot
    // fills their shared stores from the deployed assets.
    XER_VE_CHECK(!apprt::SharedPanelsConfig().panels.empty());
    XER_VE_CHECK(apprt::SharedWorkspacesConfig().size() >= 4);
}

// color.yaml Profiles: sequence — each entry is either a Lut look or a
// Source/Display/View colorspace path. An entry without a Name must be
// skipped; a Color: block with no Profiles: section returns true with
// an empty list.
void TestOcioProfilesParse() {
    const auto dir = TempRoot() / "ocio_profiles";
    std::filesystem::create_directories(dir);
    const auto path = dir / "color.yaml";
    XER_VE_CHECK(WriteFile(path,
                           "Color:\n"
                           "  Source: \"sRGB\"\n"
                           "  Profiles:\n"
                           "    - Name: \"Teal\"\n"
                           "      Lut: \"luts/x.cube\"\n"
                           "    - Name: \"ToRec709\"\n"
                           "      Display: \"sRGB\"\n"
                           "      View: \"Standard\"\n"
                           "    - Lut: \"luts/noname.cube\"\n"));
    std::vector<vecolor::OcioProfile> profiles;
    std::string error;
    XER_VE_CHECK(vecolor::LoadOcioProfiles(path, &profiles, &error));
    XER_VE_CHECK(profiles.size() == 2);  // nameless entry skipped
    if (profiles.size() == 2) {
        XER_VE_CHECK(profiles[0].name == "Teal");
        XER_VE_CHECK(profiles[0].lut_path == "luts/x.cube");
        XER_VE_CHECK(profiles[1].display == "sRGB");
    }

    // Color: present but no Profiles: -> true + empty, not an error.
    const auto bare = dir / "color_noprofiles.yaml";
    XER_VE_CHECK(WriteFile(bare,
                           "Color:\n"
                           "  Source: \"sRGB\"\n"));
    profiles.clear();
    XER_VE_CHECK(vecolor::LoadOcioProfiles(bare, &profiles, &error));
    XER_VE_CHECK(profiles.empty());
}

// A real LUT_3D_SIZE 2 invert-ramp .cube behind a Lut profile.
// OcioAvailable gates the expectations the same way TestOcioTransform
// does: when OCIO is present the profile FileTransform must apply,
// when absent the call returns false without crashing. Input is dark
// gray 48 (not mid-gray): the invert ramp's fixed point is 0.5, so
// 128 would come back ~unchanged.
void TestOcioProfileLut() {
    const auto dir = TempRoot() / "ocio_lut";
    std::filesystem::create_directories(dir);
    // Adobe .cube stores red fastest, then green, then blue.
    // Use unequal channels below so an accidental R/B swap is detected.
    XER_VE_CHECK(WriteFile(dir / "inv.cube",
                           "LUT_3D_SIZE 2\n"
                           "1.0 1.0 1.0\n"
                           "0.0 1.0 1.0\n"
                           "1.0 0.0 1.0\n"
                           "0.0 0.0 1.0\n"
                           "1.0 1.0 0.0\n"
                           "0.0 1.0 0.0\n"
                           "1.0 0.0 0.0\n"
                           "0.0 0.0 0.0\n"));
    vecolor::OcioProfile profile;
    profile.name = "inv";
    profile.lut_path = "inv.cube";  // resolves against assets_dir
    const vecolor::OcioConfig config;  // defaults -> OCIO builtin
    std::vector<std::uint8_t> bgra(2 * 2 * 4);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i] = 32;
        bgra[i + 1] = 80;
        bgra[i + 2] = 160;
        bgra[i + 3] = 73;
    }
    const bool applied = vecolor::ApplyProfileTransform(
        bgra.data(), 2, 2, 2 * 4, profile, config, dir);
#if defined(XER_VE_HAS_OCIO)
    XER_VE_CHECK(applied);
    XER_VE_CHECK(std::abs(int(bgra[0]) - 223) <= 1);
    XER_VE_CHECK(std::abs(int(bgra[1]) - 175) <= 1);
    XER_VE_CHECK(std::abs(int(bgra[2]) - 95) <= 1);
    XER_VE_CHECK(bgra[3] == 73);
#else
    XER_VE_CHECK(!applied);
#endif
}

// FindOcioProfile is a case-insensitive lookup over the process-wide
// table: a test entry pushed into SharedOcioProfiles must be found
// and cleanly removable afterwards.
void TestOcioProfileLookup() {
    vecolor::OcioProfile fake;
    fake.name = "UnitTest";
    vecolor::SharedOcioProfiles().push_back(fake);
    const vecolor::OcioProfile* found =
        vecolor::FindOcioProfile("unittest");  // lowercased on purpose
    XER_VE_CHECK(found != nullptr);
    if (found != nullptr) {
        XER_VE_CHECK(found->name == "UnitTest");
    }
    auto& shared = vecolor::SharedOcioProfiles();
    shared.erase(std::remove_if(shared.begin(), shared.end(),
                                [](const vecolor::OcioProfile& p) {
                                    return p.name == "UnitTest";
                                }),
                 shared.end());
    XER_VE_CHECK(vecolor::FindOcioProfile("unittest") == nullptr);
}

// Contract check for ocio:<name> dispatch — after Boot() loads the
// deployed assets/color.yaml its Profiles: entries must sit in the
// shared table and be findable by name. Mirrors TestBootAssetsRegistry
// (Boot is idempotent, so call it fresh here).
void TestOcioDispatchRegistered() {
    std::string status;
    BootResult result = apprt::Boot(&status);
    XER_VE_CHECK(result.ok);
    XER_VE_CHECK(!vecolor::SharedOcioProfiles().empty());
    XER_VE_CHECK(vecolor::FindOcioProfile("Teal Orange") != nullptr);
}

}  // namespace

int XER_VideoEditor_RunAppRuntimeTests() {
    std::filesystem::create_directories(TempRoot());
    const int before = XER::VideoEditor::Test::Failures();
    const struct {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"manifest_missing_file", &TestManifestMissingFile},
        {"manifest_parses_runtime_script",
         &TestManifestParsesRuntimeScript},
        {"manifest_missing_script_fails",
         &TestManifestMissingScriptFails},
        {"window_config_defaults", &TestWindowConfigDefaults},
        {"window_config_size_and_bools", &TestWindowConfigSizeAndBools},
        {"expand_env_weakeroot", &TestExpandEnvWeaklyRoot},
        {"engine_create_and_run", &TestEngineCreateAndRun},
        {"engine_outdata_via_script", &TestEngineOutDataViaScript},
        {"engine_nativeimport_modules",
         &TestEngineNativeImportModules},
        {"boot_missing_assets", &TestBootMissingAssets},
        {"fire_plugin_load_noengine", &TestFirePluginLoadNoEngine},
        {"svg_raster_basic", &TestSvgRasterBasic},
        {"svg_raster_invalid", &TestSvgRasterInvalid},
        {"icons_manifest", &TestIconsManifest},
        {"editor_config_full", &TestEditorConfigFull},
        {"editor_config_missing", &TestEditorConfigMissing},
        {"shortcuts_config", &TestShortcutsConfig},
        {"icon_pack_svg", &TestIconPackSvg},
        {"boot_assets_registry", &TestBootAssetsRegistry},
        {"toolbar_json", &TestToolbarJson},
        {"strings_ini", &TestStringsIni},
        {"theme_toml", &TestThemeToml},
        {"layout_yaml", &TestLayoutYaml},
        {"menus_yaml_full", &TestMenusYamlFull},
        {"menus_yaml_missing", &TestMenusYamlMissing},
        {"menus_yaml_malformed", &TestMenusYamlMalformed},
        {"effects_yaml_full", &TestEffectsYamlFull},
        {"inspector_yaml_full", &TestInspectorYamlFull},
        {"workspaces_yaml_full", &TestWorkspacesYamlFull},
        {"monitor_yaml_full", &TestMonitorYamlFull},
        {"panels_yaml", &TestPanelsYaml},
        {"workspaces_yaml", &TestWorkspacesConfigYaml},
        {"shared_finders", &TestSharedFinders},
        {"ocio_config", &TestOcioConfig},
        {"ocio_transform", &TestOcioTransform},
        {"oiio_roundtrip", &TestOiioRoundtrip},
        {"oiio_handles", &TestOiioHandles},
        {"ui_manifest_shared", &TestUiManifestShared},
        {"ocio_profiles_parse", &TestOcioProfilesParse},
        {"ocio_profile_lut", &TestOcioProfileLut},
        {"ocio_profile_lookup", &TestOcioProfileLookup},
        {"ocio_dispatch_registered", &TestOcioDispatchRegistered},
    };
    for (const auto& c : cases) {
        std::printf("[RUN] %s\n", c.name);
        std::fflush(stdout);
        const int case_before = XER::VideoEditor::Test::Failures();
        c.fn();
        const bool passed =
            XER::VideoEditor::Test::Failures() == case_before;
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", c.name);
        std::fflush(stdout);
    }
    std::error_code ec;
    std::filesystem::remove_all(TempRoot(), ec);
    return XER::VideoEditor::Test::Failures() - before;
}
