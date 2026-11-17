// Shared plugin-side table declaration. Each plugin DLL defines a
// static `plugin::Entry kEntries[]` table plus `kCount`, then includes
// exports_impl.h which emits the xer_plugin_* C exports from it.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace plugin {

// One exported effect. `video` handles XER_PLUGIN_VIDEO_FX effects,
// `audio` handles XER_PLUGIN_AUDIO_FX — exactly one is non-null.
struct Entry {
    const char* id;
    const char* name;
    int kind;                        // XER_PluginKind
    XER_ApplyVideoFn video;
    XER_ApplyAudioFn audio;
    const XER_ParamDesc* params;
    int param_count;
};

// Convenience: video impls declared in each plugin's effects.h use the
// XER_ApplyVideoFn signature directly (BGRA8 in place, return 0).

}  // namespace plugin
