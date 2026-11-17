// Icon pack: logical-name → SVG file mapping (assets/icons.yaml) plus
// a tinted-raster cache. Widgets ask for icons by logical name and get
// back BGRA bitmaps ready for Renderer::DrawFrameBgra.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::VideoEditor::AppRuntime {

struct IconBitmap {
    std::vector<std::uint8_t> bgra;  // width*height*4, B,G,R,A
    int width = 0;
    int height = 0;
    bool ok = false;
};

// Parses assets/icons.yaml:
//   Icons:
//     nav.media: apps.svg
//     transport.play: start.svg
// Values may be bare filenames (resolved under the icons dir) or
// relative paths.
bool LoadIconsManifest(
    const std::filesystem::path& yaml_path,
    std::unordered_map<std::string, std::string>* name_to_file,
    std::string* error);

class IconPack {
public:
    // icons_dir is where the SVG files live; `mapping` is logical →
    // file from LoadIconsManifest. Unmapped logical names fall back to
    // "<name>.svg" directly in icons_dir.
    void Configure(std::filesystem::path icons_dir,
                   std::unordered_map<std::string, std::string> mapping);

    // Rasterized icon tinted `argb` at `px`×`px`; nullptr when the icon
    // can't be found/rasterized (caller falls back to drawn glyphs).
    // Cached by (name, px, argb). NOT thread-safe — UI thread only.
    const IconBitmap* Get(const std::string& name, int px,
                          std::uint32_t argb);

    bool Has(const std::string& name) const;
    const std::filesystem::path& directory() const { return dir_; }

private:
    struct Key {
        std::string name;
        int px;
        std::uint32_t argb;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<std::string>()(k.name) ^
                   (std::hash<int>()(k.px) << 1) ^
                   (std::hash<std::uint32_t>()(k.argb) << 2);
        }
    };
    std::filesystem::path dir_;
    std::unordered_map<std::string, std::string> mapping_;
    std::unordered_map<Key, IconBitmap, KeyHash> cache_;
};

// Process-wide icon pack, configured by Boot() when assets/icons.yaml
// exists. Empty until then — widgets must tolerate null Get().
IconPack& SharedIconPack();

}  // namespace XER::VideoEditor::AppRuntime
