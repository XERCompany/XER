// XER Video Editor plugin ABI — pure C, no STL across the boundary.
// A plugin is a DLL placed in <exe>/plugins/ that exports the five
// xer_plugin_* functions below. The editor probes the directory at
// startup; each plugin registers one or more effects which become
// usable as clip effects of type "plugin:<id>" and are applied
// in-place to BGRA8 frames (video) or interleaved float PCM (audio).
#pragma once

#include <stdint.h>

#ifdef _WIN32
#define XER_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define XER_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define XER_PLUGIN_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XER_PluginKind {
    XER_PLUGIN_VIDEO_FX = 0,
    XER_PLUGIN_AUDIO_FX = 1,
} XER_PluginKind;

typedef struct XER_PluginInfo {
    uint32_t abi_version;     // must equal XER_PLUGIN_ABI_VERSION
    int32_t kind;             // XER_PluginKind
    const char* id;           // stable id: "xer.fx.sepia" -> "plugin:xer.fx.sepia"
    const char* name;         // human-readable display name
    const char* vendor;       // e.g. "XER"
    int32_t param_count;      // number of float params (0..8)
} XER_PluginInfo;

typedef struct XER_ParamDesc {
    const char* name;
    float min_value;
    float max_value;
    float default_value;
} XER_ParamDesc;

// BGRA8 frame view — 4 bytes per pixel, stride in bytes. Pixels are
// modified in place. Return 0 on success, nonzero on failure.
typedef struct XER_VideoFrame {
    uint8_t* data;
    int32_t width;
    int32_t height;
    int32_t stride;
} XER_VideoFrame;

// Interleaved float PCM (frames * channels samples), sample_rate in Hz.
typedef struct XER_AudioBuffer {
    float* samples;
    int32_t frames;
    int32_t channels;
    int32_t sample_rate;
} XER_AudioBuffer;

typedef int (*XER_ApplyVideoFn)(XER_VideoFrame* frame,
                                const float* params, int32_t param_count);
typedef int (*XER_ApplyAudioFn)(XER_AudioBuffer* buffer,
                                const float* params, int32_t param_count);

// Required exports — resolved by the host with GetProcAddress:
XER_PLUGIN_EXPORT int32_t xer_plugin_count(void);
XER_PLUGIN_EXPORT const XER_PluginInfo* xer_plugin_info(int32_t index);
XER_PLUGIN_EXPORT const XER_ParamDesc* xer_plugin_params(int32_t index,
                                                       int32_t* out_count);
XER_PLUGIN_EXPORT XER_ApplyVideoFn xer_plugin_video_fn(int32_t index);
XER_PLUGIN_EXPORT XER_ApplyAudioFn xer_plugin_audio_fn(int32_t index);

#ifdef __cplusplus
}
#endif
