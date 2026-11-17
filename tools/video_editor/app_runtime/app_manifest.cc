// Implementation of the declarative assets layer: <assets_dir>/app.yaml
// selects the runtime script; a window.yaml shapes the native window.
// Every field beyond Application.RuntimeScript is optional — absent or
// malformed keys keep the defaults declared in app_manifest.h.
#include "app_manifest.h"

#include <algorithm>
#include <cctype>

#include <yaml-cpp/yaml.h>

namespace XER::VideoEditor::AppRuntime {
namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool ParseBool(const YAML::Node& node, bool fallback) {
    if (!node.IsDefined() || !node.IsScalar()) return fallback;
    const std::string s = ToLower(node.Scalar());
    if (s == "true" || s == "1" || s == "yes") return true;
    if (s == "false" || s == "0" || s == "no") return false;
    return fallback;
}

void ReadInt(const YAML::Node& map, const char* key, int* out) {
    const YAML::Node node = map[key];
    int value = 0;
    if (node.IsDefined() && node.IsScalar() &&
        YAML::convert<int>::decode(node, value)) {
        *out = value;
    }
}

}  // namespace

bool LoadAppManifest(const std::filesystem::path& assets_dir,
                     AppManifest* out, std::string* error) {
    const std::filesystem::path yaml_path = assets_dir / "app.yaml";
    if (!std::filesystem::exists(yaml_path)) {
        if (error) *error = "app.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node app = root["Application"];
    const YAML::Node runtime =
        app.IsDefined() ? app["RuntimeScript"] : YAML::Node();
    if (!runtime.IsDefined() || !runtime.IsScalar() ||
        runtime.Scalar().empty()) {
        if (error) *error = "app.yaml: Application.RuntimeScript missing";
        return false;
    }

    out->runtime_script = runtime.Scalar();
    out->base_dir = assets_dir;

    const YAML::Node name = app["Name"];
    if (name.IsDefined() && name.IsScalar()) out->name = name.Scalar();
    const YAML::Node version = app["Version"];
    if (version.IsDefined() && version.IsScalar())
        out->version = version.Scalar();

    return true;
}

bool LoadWindowConfig(const std::filesystem::path& yaml_path,
                      WindowConfig* out, std::string* error) {
    if (!std::filesystem::exists(yaml_path)) {
        if (error) *error = "window.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node window = root["Window"];
    if (!window.IsDefined() || !window.IsMap()) {
        if (error) *error = "window.yaml: Window section missing";
        return false;
    }

    const YAML::Node size = window["Size"];
    if (size.IsDefined() && size.IsMap()) {
        ReadInt(size, "w", &out->width);
        ReadInt(size, "h", &out->height);
    }

    const YAML::Node min_size = window["MinSize"];
    if (min_size.IsDefined() && min_size.IsMap()) {
        ReadInt(min_size, "w", &out->min_width);
        ReadInt(min_size, "h", &out->min_height);
    }

    const YAML::Node title = window["Title"];
    if (title.IsDefined() && title.IsScalar()) out->title = title.Scalar();

    out->open_devtools =
        ParseBool(window["OpenDevTools"], out->open_devtools);
    out->maximized = ParseBool(window["Maximized"], out->maximized);

    return true;
}

}  // namespace XER::VideoEditor::AppRuntime
