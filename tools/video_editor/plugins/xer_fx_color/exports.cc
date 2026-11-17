#include "../common/entry.h"
#include "effects.h"

static const plugin::Entry kEntries[] = {
    {"xer.fx.hue_rotate", "Hue Rotate", XER_PLUGIN_VIDEO_FX, fx::hue_rotate, nullptr, nullptr, 0},
    {"xer.fx.sepia", "Sepia", XER_PLUGIN_VIDEO_FX, fx::sepia_tone, nullptr, nullptr, 0},
    {"xer.fx.duotone", "Duotone", XER_PLUGIN_VIDEO_FX, fx::duotone, nullptr, nullptr, 0},
    {"xer.fx.channel_mixer", "Channel Mixer", XER_PLUGIN_VIDEO_FX, fx::channel_mixer, nullptr, nullptr, 0},
    {"xer.fx.invert_lum", "Invert Luma", XER_PLUGIN_VIDEO_FX, fx::invert_lum, nullptr, nullptr, 0},
    {"xer.fx.color_isolate", "Color Isolate", XER_PLUGIN_VIDEO_FX, fx::color_isolate, nullptr, nullptr, 0},
    {"xer.fx.rgb_swap", "RGB Swap", XER_PLUGIN_VIDEO_FX, fx::rgb_swap, nullptr, nullptr, 0},
    {"xer.fx.saturation_boost", "Saturation Boost", XER_PLUGIN_VIDEO_FX, fx::saturation_boost, nullptr, nullptr, 0},
};

static const int32_t kCount =
    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));

#include "../common/exports_impl.h"
