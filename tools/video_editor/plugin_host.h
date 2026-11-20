// Runtime plugin host: scans <exe>/plugins/*.dll, loads each library,
// resolves the xer_plugin_* ABI exports (sdk/xer_plugin.h) and indexes
// every registered effect by id. ClipEffect types of the form
// "plugin:<id>" dispatch here from the compositor; audio plugins are
// applied to PCM buffers by the app layer (bounce workflows).
//
// Loading is best-effort: a DLL that fails to load or exports an
// incompatible ABI version is skipped with a logged warning — plugins
// are optional, never a startup dependency.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "sdk/xer_plugin.h"

namespace XER::VideoEditor {

struct PluginDesc {
    std::string id;          // "xer.fx.sepia" (type is "plugin:xer.fx.sepia")
    std::string name;
    std::string vendor;
    std::string file;        // DLL basename it came from
    XER_PluginKind kind = XER_PLUGIN_VIDEO_FX;
    std::vector<XER_ParamDesc> params;
    XER_ApplyVideoFn video_fn = nullptr;
    XER_ApplyAudioFn audio_fn = nullptr;
};

class PluginHost {
public:
    PluginHost() = default;
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    ~PluginHost() { Unload(); }

    // Scans <exe_dir>/plugins for *.dll and loads every compatible
    // plugin. Returns the number of effects registered. Failures are
    // collected into `warnings` (out-param may be null).
    int LoadAll(std::vector<std::string>* warnings = nullptr);
    void Unload();

    const std::deque<PluginDesc>& Effects() const { return effects_; }
    const PluginDesc* Find(const std::string& id) const;
    // Effects that make sense to list in a UI picker, by kind.
    std::vector<const PluginDesc*> OfKind(XER_PluginKind kind) const;

    // Convenience lookup by full clip-effect type — accepts both
    // "plugin:xer.fx.sepia" and the bare "xer.fx.sepia" id.
    static const PluginDesc* ForEffectType(const std::string& type);

    // Apply a video effect in place to a BGRA8 frame (width*stride
    // layout described by the caller). Returns false when the plugin
    // is missing or reports failure.
    static bool ApplyVideo(const PluginDesc& desc, void* bgra, int width,
                           int height, int stride,
                           const std::vector<float>& params);
    static bool ApplyAudio(const PluginDesc& desc, float* samples,
                           int frames, int channels, int sample_rate,
                           const std::vector<float>& params);

private:
    struct Library {
        std::string path;
        void* handle = nullptr;
    };
    std::vector<Library> libs_;
    // std::deque, not vector: push_back during LoadAll never relocates
    // existing elements — Find()/OfKind()/ForEffectType() hand out
    // `const PluginDesc*` into this container that live effect
    // sessions (compositor, audio mixer) keep holding across reloads.
    std::deque<PluginDesc> effects_;
    // Retired state kept alive for the process lifetime. Unload()
    // moves modules and descriptors here instead of freeing them:
    // sessions still holding PluginDesc*/ApplyFn pointers captured
    // before a reload must keep working — FreeLibrary would unmap
    // the code ApplyFn calls into and destroying the descriptors
    // would dangle every outstanding handle. Plugin sets are small;
    // the retention is deliberate (see Unload()).
    std::vector<Library> retired_libs_;
    std::deque<std::deque<PluginDesc>> retired_effects_;
};

// Process-wide host — loaded lazily by the compositor/app on first
// use. Not thread-safe to mutate; LoadAll happens once, reads are
// const afterwards.
PluginHost& SharedPluginHost();
bool PluginEffectsLoaded();

}  // namespace XER::VideoEditor
