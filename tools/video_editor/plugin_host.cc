#include "plugin_host.h"

#include "app_runtime/app_runtime.h"
#include "builtin_fx.h"

#include <algorithm>
#include <iterator>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace XER::VideoEditor {
namespace {

// Directory of the running executable — plugins live next to it.
std::string ExeDir() {
#ifdef _WIN32
    char path[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string exe(path, n);
    const std::size_t sep = exe.find_last_of("\\/");
    return sep == std::string::npos ? exe : exe.substr(0, sep);
#else
    return ".";
#endif
}

std::string Basename(const std::string& path) {
    const std::size_t sep = path.find_last_of("\\/");
    return sep == std::string::npos ? path : path.substr(sep + 1);
}

void* LoadLib(const std::string& path) {
#ifdef _WIN32
    return reinterpret_cast<void*>(
        LoadLibraryExA(path.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                           LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* LoadSymbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

void UnloadLib(void* lib) {
    if (lib == nullptr) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}

// Upper bound on the plugin-reported parameter count — the ABI
// declares 0..8; a plugin reporting more is malformed and must not
// drive a read of params[0..n).
constexpr int32_t kMaxPluginParams = 64;

std::vector<std::string> ListPluginDlls(const std::string& dir) {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA data{};
    const std::string pattern = dir + "\\*.dll";
    HANDLE find = FindFirstFileA(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            out.push_back(dir + "\\" + data.cFileName);
        }
    } while (FindNextFileA(find, &data) != 0);
    FindClose(find);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

int PluginHost::LoadAll(std::vector<std::string>* warnings) {
    Unload();
    // First-party builtins (builtin_fx.cc — the retired xer_fx_*
    // kernels) register before the DLL scan so the "xer.fx.*"/
    // "xer.af.*" ids resolve even with no plugins/ directory, and a
    // stale first-party DLL left behind by an older install can't
    // shadow them (id collisions below keep the builtin).
    for (const PluginDesc& desc : BuiltinFx::Effects()) {
        effects_.push_back(desc);
    }
    const std::string dir = ExeDir() + "\\plugins";
    for (const std::string& path : ListPluginDlls(dir)) {
        void* lib = LoadLib(path);
        if (lib == nullptr) {
            if (warnings != nullptr) {
                warnings->push_back("plugin load failed: " +
                                    Basename(path));
            }
            continue;
        }
        auto count_fn = reinterpret_cast<int32_t (*)()>(
            LoadSymbol(lib, "xer_plugin_count"));
        auto info_fn =
            reinterpret_cast<const XER_PluginInfo* (*)(int32_t)>(
                LoadSymbol(lib, "xer_plugin_info"));
        if (count_fn == nullptr || info_fn == nullptr) {
            if (warnings != nullptr) {
                warnings->push_back(
                    "plugin lacks xer_plugin_* exports: " +
                    Basename(path));
            }
            UnloadLib(lib);
            continue;
        }
        libs_.push_back({path, lib});
        const int32_t count = count_fn();
        auto params_fn =
            reinterpret_cast<const XER_ParamDesc* (*)(int32_t,
                                                    int32_t*)>(
                LoadSymbol(lib, "xer_plugin_params"));
        auto video_fn = reinterpret_cast<XER_ApplyVideoFn (*)(int32_t)>(
            LoadSymbol(lib, "xer_plugin_video_fn"));
        auto audio_fn = reinterpret_cast<XER_ApplyAudioFn (*)(int32_t)>(
            LoadSymbol(lib, "xer_plugin_audio_fn"));
        for (int32_t i = 0; i < count; ++i) {
            const XER_PluginInfo* info = info_fn(i);
            if (info == nullptr || info->id == nullptr ||
                info->abi_version != XER_PLUGIN_ABI_VERSION) {
                if (warnings != nullptr) {
                    warnings->push_back(
                        "plugin effect " + std::to_string(i) + " in " +
                        Basename(path) + " has incompatible ABI");
                }
                continue;
            }
            PluginDesc desc;
            desc.id = info->id;
            desc.name = info->name != nullptr ? info->name : info->id;
            desc.vendor = info->vendor != nullptr ? info->vendor : "";
            desc.file = Basename(path);
            desc.kind = static_cast<XER_PluginKind>(info->kind);
            bool params_ok = true;
            if (params_fn != nullptr) {
                int32_t n = 0;
                const XER_ParamDesc* params = params_fn(i, &n);
                // The plugin reports both the pointer and the count —
                // a malformed plugin can hand back nullptr (or a short
                // buffer) with n > 0, which would dereference straight
                // into unmapped memory. Validate before copying.
                if (n < 0 || n > kMaxPluginParams ||
                    (n > 0 && params == nullptr)) {
                    params_ok = false;
                } else if (params != nullptr) {
                    for (int32_t p = 0; p < n; ++p) {
                        if (params[p].name == nullptr) {
                            params_ok = false;
                            break;
                        }
                    }
                    if (params_ok) {
                        desc.params.assign(params, params + n);
                    }
                }
            }
            if (!params_ok) {
                if (warnings != nullptr) {
                    warnings->push_back(
                        "plugin effect '" + desc.id + "' in " +
                        Basename(path) +
                        " reported a malformed param table; skipped");
                }
                continue;
            }
            if (video_fn != nullptr) desc.video_fn = video_fn(i);
            if (audio_fn != nullptr) desc.audio_fn = audio_fn(i);
            if ((desc.kind == XER_PLUGIN_VIDEO_FX &&
                 desc.video_fn == nullptr) ||
                (desc.kind == XER_PLUGIN_AUDIO_FX &&
                 desc.audio_fn == nullptr)) {
                continue;  // effect with no entry point — skip silently
            }
            // First match wins: a DLL effect whose id is already
            // claimed (by a builtin or an earlier plugin) is skipped —
            // keeps the picker clean when a retired xer_fx_*.dll is
            // still sitting next to the exe.
            if (Find(desc.id) != nullptr) {
                continue;
            }
            // runtime.js-registered handler (if the assets layer booted)
            // sees each effect as {path, factory:{name,version,features}}.
            AppRuntime::FirePluginLoad(AppRuntime::SharedBootEngine(),
                                       path, desc.name, desc.vendor,
                                       desc.id);
            effects_.push_back(std::move(desc));
        }
    }
    return static_cast<int>(effects_.size());
}

void PluginHost::Unload() {
    // Retire, don't free. Callers (VideoEffectSession,
    // AudioEffectChainSession) hold `const PluginDesc*` and
    // ApplyFn/ProcessFn pointers obtained from Find()/OfKind() — the
    // descriptors live in effects_ and the entry points live inside
    // the mapped DLL images. FreeLibrary + effects_.clear() would
    // leave those sessions calling into unmapped code and
    // dereferencing freed memory. There is no refcount over sessions,
    // so modules and descriptors stay resident until process exit
    // (the OS reclaims them); the retained footprint is a handful of
    // small DLLs at most.
    retired_libs_.insert(retired_libs_.end(),
                         std::make_move_iterator(libs_.begin()),
                         std::make_move_iterator(libs_.end()));
    libs_.clear();
    // Moving the deque into retired_effects_ is constant-time and
    // preserves element addresses — outstanding PluginDesc* stay
    // valid across a LoadAll reload.
    retired_effects_.push_back(std::move(effects_));
    effects_.clear();
}

const PluginDesc* PluginHost::Find(const std::string& id) const {
    for (const PluginDesc& desc : effects_) {
        if (desc.id == id) return &desc;
    }
    return nullptr;
}

std::vector<const PluginDesc*> PluginHost::OfKind(
    XER_PluginKind kind) const {
    std::vector<const PluginDesc*> out;
    for (const PluginDesc& desc : effects_) {
        if (desc.kind == kind) out.push_back(&desc);
    }
    return out;
}

const PluginDesc* PluginHost::FindBuiltin(const std::string& id) const {
    for (const PluginDesc& desc : effects_) {
        if (desc.builtin && desc.id == id) return &desc;
    }
    return nullptr;
}

const PluginDesc* PluginHost::ForEffectType(const std::string& type) {
    static constexpr const char* kPluginPrefix = "plugin:";
    static constexpr const char* kBuiltinPrefix = "builtin:";
    if (type.rfind(kPluginPrefix, 0) == 0) {
        return SharedPluginHost().Find(type.substr(7));
    }
    if (type.rfind(kBuiltinPrefix, 0) == 0) {
        return SharedPluginHost().FindBuiltin(type.substr(8));
    }
    // Bare id — documented lookup form, resolves like "plugin:<id>".
    return SharedPluginHost().Find(type);
}

bool PluginHost::ApplyVideo(const PluginDesc& desc, void* bgra,
                            int width, int height, int stride,
                            const std::vector<float>& params) {
    if (desc.video_fn == nullptr || bgra == nullptr) return false;
    XER_VideoFrame frame{static_cast<uint8_t*>(bgra), width, height,
                         stride};
    return desc.video_fn(&frame, params.data(),
                         static_cast<int32_t>(params.size())) == 0;
}

bool PluginHost::ApplyAudio(const PluginDesc& desc, float* samples,
                            int frames, int channels, int sample_rate,
                            const std::vector<float>& params) {
    if (desc.audio_fn == nullptr || samples == nullptr) return false;
    XER_AudioBuffer buffer{samples, frames, channels, sample_rate};
    return desc.audio_fn(&buffer, params.data(),
                         static_cast<int32_t>(params.size())) == 0;
}

PluginHost& SharedPluginHost() {
    static PluginHost host;
    static std::once_flag once;
    std::call_once(once, [] {
        std::vector<std::string> warnings;
        host.LoadAll(&warnings);
    });
    return host;
}

bool PluginEffectsLoaded() {
    return !SharedPluginHost().Effects().empty();
}

}  // namespace XER::VideoEditor
