// Loads assets/editor.yaml and assets/shortcuts.yaml into EditorConfig /
// ShortcutsConfig. See editor_config.h for the on-disk schema. Every field
// is optional — absent keys leave the caller-provided defaults untouched.
#include "editor_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace XER::VideoEditor::AppRuntime {
namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Reads `node[key]` into `*value`. Returns false when the key is absent, is
// not a scalar, or fails YAML::convert decode — a bad value never throws.
// node[key] on a zombie/undefined node (missing parent key) throws
// YAML::InvalidNode — IsDefined() is the only throw-free probe, so it
// must gate every keyed lookup. IsMap() is safe once defined.
bool ReadInt(const YAML::Node& node, const char* key, int* value) {
    if (!node.IsDefined() || !node.IsMap()) return false;
    const YAML::Node child = node[key];
    if (!child.IsDefined() || !child.IsScalar()) return false;
    return YAML::convert<int>::decode(child, *value);
}

bool ReadBool(const YAML::Node& node, const char* key, bool* value) {
    if (!node.IsDefined() || !node.IsMap()) return false;
    const YAML::Node child = node[key];
    if (!child.IsDefined() || !child.IsScalar()) return false;
    return YAML::convert<bool>::decode(child, *value);
}

bool ReadStr(const YAML::Node& node, const char* key,
             std::string* value) {
    if (!node.IsDefined() || !node.IsMap()) return false;
    const YAML::Node child = node[key];
    if (!child.IsDefined() || !child.IsScalar()) return false;
    return YAML::convert<std::string>::decode(child, *value);
}

// "#RRGGBB" or "#AARRGGBB" (yaml may quote it) -> 32-bit ARGB.
// RRGGBB gets opaque alpha 0xFF. Parsed manually with strtoul.
bool ParseAccentArgb(const std::string& text, std::uint32_t* out) {
    if (text.size() != 7 && text.size() != 9) return false;
    if (text.front() != '#') return false;
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str() + 1, &end, 16);
    if (end == nullptr || *end != '\0') return false;
    *out = text.size() == 7 ? (0xFF000000u | static_cast<std::uint32_t>(value))
                            : static_cast<std::uint32_t>(value);
    return true;
}

}  // namespace

bool LoadEditorConfig(const std::filesystem::path& yaml_path,
                      EditorConfig* out, std::string* error) {
    if (!out) return false;
    if (!std::filesystem::exists(yaml_path)) {
        if (error) *error = "editor.yaml not found";
        return false;
    }
    try {
        const YAML::Node root = YAML::LoadFile(yaml_path.string());
        const YAML::Node editor = root["Editor"];
        if (!editor.IsDefined() || !editor.IsMap()) {
            if (error) *error = "editor.yaml: Editor section missing";
            return false;
        }

        std::string s;
        if (ReadStr(editor, "Theme", &s)) {
            s = ToLower(s);
            if (s == "dark" || s == "light") out->theme = s;
        }
        if (ReadStr(editor, "Accent", &s)) {
            std::uint32_t argb = 0;
            if (ParseAccentArgb(s, &argb)) out->accent_argb = argb;
        }
        int iv = 0;
        if (ReadInt(editor, "RightTab", &iv)) {
            out->right_tab = std::clamp(iv, 0, 9);   // 0..9 tabs
        } else if (editor["RightTab"].IsDefined()) {
            out->right_tab = -1;  // present but not a valid int
        }
        bool bv = false;
        const YAML::Node panels = editor["Panels"];
        if (ReadBool(panels, "Left", &bv)) out->left_panel = bv;
        if (ReadBool(panels, "Right", &bv)) out->right_panel = bv;
        const YAML::Node timeline = editor["Timeline"];
        if (ReadInt(timeline, "Height", &iv)) {
            out->timeline_height = std::clamp(iv, 40, 600);
        }
        const YAML::Node waveform = editor["Waveform"];
        if (ReadInt(waveform, "Height", &iv)) {
            out->waveform_height = std::clamp(iv, 40, 600);
        }
        const YAML::Node media_bin = editor["MediaBin"];
        if (ReadStr(media_bin, "View", &s)) {
            s = ToLower(s);
            if (s == "grid" || s == "list") out->media_bin_view = s;
        }
        if (ReadBool(editor, "StatusBar", &bv)) out->show_status_bar = bv;
        return true;
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool LoadShortcutsConfig(const std::filesystem::path& yaml_path,
                         ShortcutsConfig* out, std::string* error) {
    if (!out) return false;
    if (!std::filesystem::exists(yaml_path)) {
        if (error) *error = "shortcuts.yaml not found";
        return false;
    }
    try {
        const YAML::Node root = YAML::LoadFile(yaml_path.string());
        const YAML::Node shortcuts = root["Shortcuts"];
        if (!shortcuts.IsDefined() || !shortcuts.IsMap()) {
            if (error) *error = "shortcuts.yaml: Shortcuts section missing";
            return false;
        }
        for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it) {
            if (!it->first.IsScalar() || !it->second.IsScalar()) continue;
            out->keys[it->first.as<std::string>()] =
                it->second.as<std::string>();
        }
        return true;
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

}  // namespace XER::VideoEditor::AppRuntime
