// XER Studio bootstrap — runs in the embedded V8 at app start.
// NativeImport resolves host-provided modules; setOutAppData hands
// config + event handlers back to the native side.
const winkernel = NativeImport("app_super_winkernel");
const concat = NativeImport("app_super_concat");
const exports = NativeImport("app_super_exportAppData");

const ExeRoot = winkernel.SiEnvironmentMatchPath(
    "GET", "%WEAKLY_ROOT_PATH_EXECUTE%");

const asset = (...parts) =>
    concat.handle({ join: "/" }, ExeRoot, "assets", ...parts);

exports.setOutAppData({
    [exports.ENUM.CONFIG_WINDOW]: asset("window", "window.yaml"),
    [exports.ENUM.CONFIG_ICONS]: asset("icons.yaml"),
    [exports.ENUM.CONFIG_ICONS_DIR]: asset("icons"),
    [exports.ENUM.CONFIG_EDITOR]: asset("editor.yaml"),
    [exports.ENUM.CONFIG_SHORTCUTS]: asset("shortcuts.yaml"),
    [exports.ENUM.CONFIG_TOOLBAR]: asset("ui", "toolbar.json"),
    [exports.ENUM.CONFIG_STRINGS]: asset("ui", "strings.ini"),
    [exports.ENUM.CONFIG_THEME]: asset("ui", "theme.toml"),
    [exports.ENUM.CONFIG_LAYOUT]: asset("ui", "layout.yaml"),
    [exports.ENUM.CONFIG_PANELS]: asset("ui", "panels.yaml"),
    [exports.ENUM.CONFIG_WORKSPACES]: asset("ui", "workspaces.yaml"),
    [exports.ENUM.CONFIG_COLOR]: asset("color.yaml"),

    [exports.ENUM.FUNCTION.HANDLER_EVENT_LOAD_PLUGIN]: function (data) {
        const { path, factory: { name, version, features } } = data;
        console.log("plugin: " + name + " (" + version + ") " +
                    features + " — " + path);
    }
});
