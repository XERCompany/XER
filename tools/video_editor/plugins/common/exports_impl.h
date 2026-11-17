// Emits the five xer_plugin_* C exports from the static table each
// plugin defines. Usage — in the plugin's exports.cc:
//
//   #include "../common/entry.h"
//   #include "effects.h"
//   static const plugin::Entry kEntries[] = { ... };
//   static const int32_t kCount =
//       static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));
//   #include "../common/exports_impl.h"
#pragma once

#include "../common/entry.h"

namespace {
XER_PluginInfo g_info[64];
}

XER_PLUGIN_EXPORT int32_t xer_plugin_count(void) { return kCount; }

XER_PLUGIN_EXPORT const XER_PluginInfo* xer_plugin_info(int32_t index) {
    if (index < 0 || index >= kCount || index >= 64) return nullptr;
    const plugin::Entry& e = kEntries[index];
    g_info[index].abi_version = XER_PLUGIN_ABI_VERSION;
    g_info[index].kind = e.kind;
    g_info[index].id = e.id;
    g_info[index].name = e.name;
    g_info[index].vendor = "XER";
    g_info[index].param_count = e.param_count;
    return &g_info[index];
}

XER_PLUGIN_EXPORT const XER_ParamDesc* xer_plugin_params(
    int32_t index, int32_t* out_count) {
    if (out_count != nullptr) *out_count = 0;
    if (index < 0 || index >= kCount) return nullptr;
    if (out_count != nullptr) *out_count = kEntries[index].param_count;
    return kEntries[index].params;
}

XER_PLUGIN_EXPORT XER_ApplyVideoFn xer_plugin_video_fn(int32_t index) {
    if (index < 0 || index >= kCount) return nullptr;
    return kEntries[index].video;
}

XER_PLUGIN_EXPORT XER_ApplyAudioFn xer_plugin_audio_fn(int32_t index) {
    if (index < 0 || index >= kCount) return nullptr;
    return kEntries[index].audio;
}
