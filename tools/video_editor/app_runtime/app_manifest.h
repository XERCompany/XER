// App manifest + window config — the declarative assets layer.
// assets/app.yaml declares the runtime script; assets/window/window.yaml
// declares the native window's geometry/title. Loaded via yaml-cpp
// (system package, already used by XER_ContentAI).
#pragma once

#include <filesystem>
#include <string>

namespace XER::VideoEditor::AppRuntime {

// assets/window/window.yaml
struct WindowConfig {
    std::string title = "VideoEditor - XER Creative Suite";
    int width = 1440;
    int height = 900;
    int min_width = 0;    // 0 = unconstrained
    int min_height = 0;
    bool open_devtools = false;
    bool maximized = false;
};

// assets/app.yaml — the application manifest. The only required key is
// Application.RuntimeScript; everything else is optional metadata.
struct AppManifest {
    std::string runtime_script;      // e.g. "./runtime.js"
    std::filesystem::path base_dir;  // directory containing app.yaml
    std::string name;                // optional display name
    std::string version;             // optional version string
};

// Parses <assets_dir>/app.yaml. Fails if the file is missing or
// RuntimeScript is absent/empty. base_dir is always set to assets_dir.
bool LoadAppManifest(const std::filesystem::path& assets_dir,
                     AppManifest* out, std::string* error);

// Parses a window.yaml of the shape
//   Window:
//     Size: {w: 1560, h: 900}
//     Title: "Video Editor"
//     OpenDevTools: "true"
// Missing keys keep the defaults above. Boolean-ish strings
// ("true"/"false"/"1"/"0") are accepted for OpenDevTools.
bool LoadWindowConfig(const std::filesystem::path& yaml_path,
                      WindowConfig* out, std::string* error);

}  // namespace XER::VideoEditor::AppRuntime
