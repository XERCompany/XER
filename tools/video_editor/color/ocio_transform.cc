// Display color management via OpenColorIO — see ocio_transform.h for
// the assets/color.yaml schema. The whole feature is compile-gated on
// the OCIO headers being visible (__has_include — the package shim is
// absent on non-MSYS2 builds): without them every entry point degrades
// to false/"ocio unavailable" and frames pass through untouched.
//
// Runtime path: OcioConfig -> cached DisplayViewTransform CPU processor
// (keyed by config_path|source|display|view behind a mutex, built once
// per unique cfg) -> per-row BGRA8 <-> float-RGBA conversion around
// PackedImageDesc::apply. Alpha bytes are never rewritten.
#include "ocio_transform.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(__has_include)
#if __has_include(<OpenColorIO/OpenColorIO.h>)
#include <OpenColorIO/OpenColorIO.h>
// OCIO ships only the versioned OCIO_NAMESPACE; the OCIO:: alias is the
// embedder's job (see the usage example inside OpenColorIO.h itself).
namespace OCIO = OCIO_NAMESPACE;
#define XER_VE_HAVE_OCIO_H 1
#endif
#endif

namespace XER::VideoEditor::Color {
namespace {

// Reads `node[key]` into `*value` when the key exists, is a scalar, and
// decodes — a missing/bad value leaves the caller default untouched.
// Not OCIO-gated: LoadOcioProfiles is yaml-only and needs it too.
bool ReadStr(const YAML::Node& node, const char* key, std::string* value) {
    const YAML::Node child = node[key];
    if (!child.IsDefined() || !child.IsScalar()) return false;
    return YAML::convert<std::string>::decode(child, *value);
}

#if defined(XER_VE_HAVE_OCIO_H)

// Same scalar-read helper for bool — kept under the gate: only
// LoadOcioConfig's OCIO branch consumes it, so an OCIO-less build would
// otherwise warn about an unused function.
bool ReadBool(const YAML::Node& node, const char* key, bool* value) {
    const YAML::Node child = node[key];
    if (!child.IsDefined() || !child.IsScalar()) return false;
    return YAML::convert<bool>::decode(child, *value);
}

// One cached CPU processor per unique (config, source, display, view).
// A null cpu means "resolution attempted and failed" — negative results
// are cached too so a bad config does not retry every frame.
struct Entry {
    OCIO::ConstCPUProcessorRcPtr cpu;
    std::string key;
};

std::mutex& CacheMutex() {
    static std::mutex m;
    return m;
}

std::map<std::string, Entry>& Cache() {
    static std::map<std::string, Entry> cache;
    return cache;
}

std::string CacheKey(const OcioConfig& cfg) {
    return cfg.config_path + "|" + cfg.source + "|" + cfg.display + "|" +
           cfg.view;
}

// Resolves the OCIO config object for `cfg` — an empty path honors the
// OCIO env var when set, else falls back to the builtin raw config.
// Throws OCIO::Exception on failure; callers wrap it in try/catch.
OCIO::ConstConfigRcPtr LoadConfig(const OcioConfig& cfg) {
    if (cfg.config_path.empty()) {
        // No explicit .ocio: honor the OCIO env var when set, else
        // fall back to the builtin raw config.
        return std::getenv("OCIO") != nullptr
                   ? OCIO::Config::CreateFromEnv()
                   : OCIO::Config::CreateRaw();
    }
    return OCIO::Config::CreateFromFile(cfg.config_path.c_str());
}

// Builds the DisplayViewTransform CPU processor for `cfg`. Never throws:
// OCIO reports every failure (missing file, unknown colorspace/display/
// view) via OCIO::Exception — any throw yields a null-cpu entry.
Entry BuildEntry(const OcioConfig& cfg) {
    Entry entry;
    try {
        const OCIO::ConstConfigRcPtr config = LoadConfig(cfg);
        const OCIO::DisplayViewTransformRcPtr transform =
            OCIO::DisplayViewTransform::Create();
        transform->setSrc(cfg.source.c_str());
        transform->setDisplay(cfg.display.c_str());
        transform->setView(cfg.view.c_str());
        const OCIO::ConstProcessorRcPtr processor =
            config->getProcessor(transform);
        if (processor) {
            entry.cpu = processor->getDefaultCPUProcessor();
        }
    } catch (const OCIO::Exception&) {
        entry.cpu.reset();
    } catch (const std::exception&) {
        entry.cpu.reset();
    } catch (...) {
        entry.cpu.reset();
    }
    return entry;
}

// Cached lookup — returns the resolved CPU processor, or nullptr when
// this cfg cannot be resolved (absent runtime, bad config, unknown
// colorspace/display/view names).
OCIO::ConstCPUProcessorRcPtr CachedCpu(const OcioConfig& cfg) {
    const std::string key = CacheKey(cfg);
    std::lock_guard<std::mutex> lock(CacheMutex());
    std::map<std::string, Entry>& cache = Cache();
    const auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second.cpu;
    }
    Entry entry = BuildEntry(cfg);
    entry.key = key;
    const OCIO::ConstCPUProcessorRcPtr cpu = entry.cpu;
    cache.emplace(key, std::move(entry));
    return cpu;
}

// Builds the CPU processor for a named profile: a FileTransform when
// `lut_src` is non-empty (resolved path to .cube/.3dl/etc.), else a
// DisplayViewTransform through the resolved source/display/view. Same
// never-throws contract as BuildEntry — a missing LUT file, unknown
// colorspace or bad config all yield a null-cpu entry.
Entry BuildProfileEntry(const OcioConfig& cfg, const std::string& lut_src,
                        const std::string& source, const std::string& display,
                        const std::string& view) {
    Entry entry;
    try {
        const OCIO::ConstConfigRcPtr config = LoadConfig(cfg);
        OCIO::ConstProcessorRcPtr processor;
        if (!lut_src.empty()) {
            const OCIO::FileTransformRcPtr transform =
                OCIO::FileTransform::Create();
            transform->setSrc(lut_src.c_str());
            processor = config->getProcessor(transform);
        } else {
            const OCIO::DisplayViewTransformRcPtr transform =
                OCIO::DisplayViewTransform::Create();
            transform->setSrc(source.c_str());
            transform->setDisplay(display.c_str());
            transform->setView(view.c_str());
            processor = config->getProcessor(transform);
        }
        if (processor) {
            entry.cpu = processor->getDefaultCPUProcessor();
        }
    } catch (const OCIO::Exception&) {
        entry.cpu.reset();
    } catch (const std::exception&) {
        entry.cpu.reset();
    } catch (...) {
        entry.cpu.reset();
    }
    return entry;
}

// Cached lookup for profile processors — shares Cache()/CacheMutex()
// with the display path under a "profile|"-prefixed key so the two
// namespaces can never alias. Negative results are cached the same way.
OCIO::ConstCPUProcessorRcPtr CachedProfileCpu(
    const OcioProfile& profile, const OcioConfig& cfg,
    const std::filesystem::path& assets_dir) {
    // Empty profile fields inherit the display-path defaults from cfg.
    const std::string source =
        profile.source.empty() ? cfg.source : profile.source;
    const std::string display =
        profile.display.empty() ? cfg.display : profile.display;
    const std::string view = profile.view.empty() ? cfg.view : profile.view;
    // LUT paths resolve relative to assets_dir; "" marks the colorspace
    // path (DisplayViewTransform) instead of a FileTransform.
    std::filesystem::path lut_path;
    if (!profile.lut_path.empty()) {
        lut_path = std::filesystem::path(profile.lut_path);
        if (lut_path.is_relative()) {
            lut_path = assets_dir / lut_path;
        }
    }
    const std::string lut_str = lut_path.generic_string();
    const std::string key = "profile|" + assets_dir.generic_string() + "|" +
                            profile.name + "|" + lut_str + "|" + source +
                            "|" + display + "|" + view + "|" +
                            cfg.config_path;
    std::lock_guard<std::mutex> lock(CacheMutex());
    std::map<std::string, Entry>& cache = Cache();
    const auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second.cpu;
    }
    Entry entry = BuildProfileEntry(cfg, lut_str, source, display, view);
    entry.key = key;
    const OCIO::ConstCPUProcessorRcPtr cpu = entry.cpu;
    cache.emplace(key, std::move(entry));
    return cpu;
}

std::uint8_t FloatToByte(float v) {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// Applies `cpu` in place on a BGRA8 frame: per row, BGRA8 -> reusable
// float-RGBA scratch -> PackedImageDesc::apply -> BGRA8. The alpha byte
// is never rewritten. Returns false when apply throws.
bool ApplyCpu(const OCIO::ConstCPUProcessorRcPtr& cpu, std::uint8_t* bgra,
              int width, int height, int stride_bytes) {
    const int row_stride = stride_bytes > 0 ? stride_bytes : width * 4;
    // One reusable float-RGBA scratch row; OCIO works in-place on it.
    std::vector<float> row(static_cast<std::size_t>(width) * 4);
    try {
        for (int y = 0; y < height; ++y) {
            std::uint8_t* px =
                bgra + static_cast<std::size_t>(y) * row_stride;
            for (int x = 0; x < width; ++x) {
                row[4 * x + 0] = px[4 * x + 2] * (1.0f / 255.0f);  // R
                row[4 * x + 1] = px[4 * x + 1] * (1.0f / 255.0f);  // G
                row[4 * x + 2] = px[4 * x + 0] * (1.0f / 255.0f);  // B
                row[4 * x + 3] = px[4 * x + 3] * (1.0f / 255.0f);  // A
            }
            // Tightly packed F32 RGBA row: channel stride sizeof(float),
            // pixel stride 4 floats, row stride width floats.
            OCIO::PackedImageDesc desc(
                row.data(), width, 1, 4, OCIO::BIT_DEPTH_F32,
                static_cast<ptrdiff_t>(sizeof(float)),
                static_cast<ptrdiff_t>(4 * sizeof(float)),
                static_cast<ptrdiff_t>(width) * 4 *
                    static_cast<ptrdiff_t>(sizeof(float)));
            cpu->apply(desc);
            for (int x = 0; x < width; ++x) {
                px[4 * x + 0] = FloatToByte(row[4 * x + 2]);
                px[4 * x + 1] = FloatToByte(row[4 * x + 1]);
                px[4 * x + 2] = FloatToByte(row[4 * x + 0]);
                // Alpha byte untouched — OCIO treats channel 3 as alpha;
                // the original byte is preserved regardless.
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

#endif  // XER_VE_HAVE_OCIO_H

}  // namespace

bool LoadOcioConfig(const std::filesystem::path& yaml_path,
                    OcioConfig* out, std::string* error) {
    if (out == nullptr) return false;
#if defined(XER_VE_HAVE_OCIO_H)
    if (!std::filesystem::exists(yaml_path)) {
        if (error) *error = "color.yaml not found";
        return false;
    }
    try {
        const YAML::Node root = YAML::LoadFile(yaml_path.string());
        const YAML::Node color = root["Color"];
        if (!color.IsDefined() || !color.IsMap()) {
            if (error) *error = "color.yaml: Color section missing";
            return false;
        }
        // Every key is optional — absent keys keep the OcioConfig
        // defaults baked into the struct.
        std::string s;
        if (ReadStr(color, "OcioConfig", &s)) out->config_path = s;
        if (ReadStr(color, "Source", &s)) out->source = s;
        if (ReadStr(color, "Display", &s)) out->display = s;
        if (ReadStr(color, "View", &s)) out->view = s;
        bool b = false;
        if (ReadBool(color, "Enabled", &b)) out->enabled = b;
        return true;
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }
#else
    (void)yaml_path;
    if (error) *error = "ocio unavailable";
    return false;
#endif
}

bool ApplyDisplayTransform(std::uint8_t* bgra, int width, int height,
                           int stride_bytes, const OcioConfig& cfg) {
    if (!cfg.enabled) return true;  // disabled is an explicit no-op
    if (bgra == nullptr || width <= 0 || height <= 0) return false;
#if defined(XER_VE_HAVE_OCIO_H)
    const OCIO::ConstCPUProcessorRcPtr cpu = CachedCpu(cfg);
    if (!cpu) return false;
    return ApplyCpu(cpu, bgra, width, height, stride_bytes);
#else
    (void)bgra;
    (void)width;
    (void)height;
    (void)stride_bytes;
    (void)cfg;
    return false;
#endif
}

bool OcioAvailable(const OcioConfig& cfg) {
#if defined(XER_VE_HAVE_OCIO_H)
    return CachedCpu(cfg) != nullptr;
#else
    (void)cfg;
    return false;
#endif
}

// ---- named profiles (see the "named profiles" contract in the header)

// yaml-only — deliberately NOT OCIO-gated: the profile table feeds the
// Color tab / ocio:<name> lookups, which still resolve on OCIO-less
// builds (ApplyProfileTransform is the entry point that degrades).
bool LoadOcioProfiles(const std::filesystem::path& yaml_path,
                      std::vector<OcioProfile>* out, std::string* error) {
    if (out == nullptr) return false;
    out->clear();
    if (!std::filesystem::exists(yaml_path)) {
        if (error) *error = "color.yaml not found";
        return false;
    }
    try {
        const YAML::Node root = YAML::LoadFile(yaml_path.string());
        const YAML::Node color = root["Color"];
        if (!color.IsDefined() || !color.IsMap()) {
            if (error) *error = "color.yaml: Color section missing";
            return false;
        }
        const YAML::Node profiles = color["Profiles"];
        if (!profiles.IsDefined() || !profiles.IsSequence()) {
            // Additive feature — an absent Profiles section is a valid
            // empty table, not an error.
            return true;
        }
        for (const YAML::Node& node : profiles) {
            if (!node.IsMap()) continue;  // malformed entry — skip
            OcioProfile profile;
            if (!ReadStr(node, "Name", &profile.name) ||
                profile.name.empty()) {
                continue;  // Name is required
            }
            // Everything else is optional; absent keys keep "" so the
            // apply path can fall back to the OcioConfig defaults.
            ReadStr(node, "Lut", &profile.lut_path);
            ReadStr(node, "Source", &profile.source);
            ReadStr(node, "Display", &profile.display);
            ReadStr(node, "View", &profile.view);
            out->push_back(std::move(profile));
        }
        return true;
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::vector<OcioProfile>& SharedOcioProfiles() {
    static std::vector<OcioProfile> profiles;
    return profiles;
}

const OcioProfile* FindOcioProfile(const std::string& name) {
    for (const OcioProfile& profile : SharedOcioProfiles()) {
        if (profile.name.size() == name.size() &&
            std::equal(profile.name.begin(), profile.name.end(),
                       name.begin(), [](char a, char b) {
                           return std::tolower(
                                      static_cast<unsigned char>(a)) ==
                                  std::tolower(
                                      static_cast<unsigned char>(b));
                       })) {
            return &profile;
        }
    }
    return nullptr;
}

bool ApplyProfileTransform(std::uint8_t* bgra, int width, int height,
                           int stride_bytes, const OcioProfile& profile,
                           const OcioConfig& cfg,
                           const std::filesystem::path& assets_dir) {
    if (!cfg.enabled) return true;  // disabled is an explicit no-op
    if (bgra == nullptr || width <= 0 || height <= 0) return false;
#if defined(XER_VE_HAVE_OCIO_H)
    const OCIO::ConstCPUProcessorRcPtr cpu =
        CachedProfileCpu(profile, cfg, assets_dir);
    if (!cpu) return false;
    return ApplyCpu(cpu, bgra, width, height, stride_bytes);
#else
    (void)bgra;
    (void)width;
    (void)height;
    (void)stride_bytes;
    (void)profile;
    (void)cfg;
    (void)assets_dir;
    return false;
#endif
}

}  // namespace XER::VideoEditor::Color
