// Implementation of the icon pack: assets/icons.yaml maps logical icon
// names ("nav.media", "transport.play") onto SVG files under the icons
// directory. Get() rasterizes on demand via RasterizeSvgIcon and caches
// the tinted BGRA bitmap per (name, size, tint); misses are cached as
// ok=false markers so a bad icon doesn't hit disk every frame.
#include "icon_pack.h"

#include "svg_raster.h"

#include <fstream>
#include <filesystem>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace XER::VideoEditor::AppRuntime {
namespace {

// Reads an entire file into a string; false when the file can't be
// opened or is empty.
bool ReadWholeFile(const std::filesystem::path& path, std::string* out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamsize size = in.tellg();
    if (size <= 0) return false;
    in.seekg(0, std::ios::beg);
    out->resize(static_cast<size_t>(size));
    if (!in.read(out->data(), size)) {
        out->clear();
        return false;
    }
    return true;
}

}  // namespace

bool LoadIconsManifest(const std::filesystem::path& yaml_path,
                       std::unordered_map<std::string, std::string>*
                           name_to_file,
                       std::string* error) {
    if (!std::filesystem::exists(yaml_path)) {
        if (error) *error = "icons.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node icons = root["Icons"];
    if (!icons.IsDefined() || !icons.IsMap()) {
        if (error) *error = "icons.yaml: Icons section missing";
        return false;
    }

    for (const auto& entry : icons) {
        if (!entry.first.IsScalar() || !entry.second.IsScalar()) continue;
        (*name_to_file)[entry.first.Scalar()] = entry.second.Scalar();
    }
    return true;
}

void IconPack::Configure(
    std::filesystem::path icons_dir,
    std::unordered_map<std::string, std::string> mapping) {
    dir_ = std::move(icons_dir);
    mapping_ = std::move(mapping);
    cache_.clear();
}

const IconBitmap* IconPack::Get(const std::string& name, int px,
                                std::uint32_t argb) {
    if (px <= 0) return nullptr;

    const Key key{name, px, argb};
    // unordered_map is node-based: references stay stable across
    // insertions, so handing out &entry is safe for the pack's lifetime.
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second.ok ? &it->second : nullptr;
    }

    const auto mapping_it = mapping_.find(name);
    const std::string file =
        mapping_it != mapping_.end() ? mapping_it->second : name + ".svg";
    const std::filesystem::path path = dir_ / file;

    IconBitmap bmp;
    std::string svg;
    if (ReadWholeFile(path, &svg) &&
        RasterizeSvgIcon(svg, px, argb, &bmp.bgra)) {
        bmp.width = px;
        bmp.height = px;
        bmp.ok = true;
    }
    // Cache the result either way — an ok=false marker for failures so
    // a missing/broken icon isn't re-read every frame.
    auto [stored, inserted] = cache_.emplace(key, std::move(bmp));
    (void)inserted;
    return stored->second.ok ? &stored->second : nullptr;
}

bool IconPack::Has(const std::string& name) const {
    if (mapping_.find(name) != mapping_.end()) return true;
    std::error_code ec;
    return std::filesystem::exists(dir_ / (name + ".svg"), ec);
}

IconPack& SharedIconPack() {
    static IconPack pack;
    return pack;
}

}  // namespace XER::VideoEditor::AppRuntime
