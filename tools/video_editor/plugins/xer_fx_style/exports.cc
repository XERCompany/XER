#include "../common/entry.h"
#include "effects.h"

static const plugin::Entry kEntries[] = {
    {"xer.fx.solarize", "Solarize", XER_PLUGIN_VIDEO_FX, fx::solarize, nullptr, nullptr, 0},
    {"xer.fx.threshold_bw", "Threshold B/W", XER_PLUGIN_VIDEO_FX, fx::threshold_bw, nullptr, nullptr, 0},
    {"xer.fx.scanlines", "Scanlines", XER_PLUGIN_VIDEO_FX, fx::scanlines, nullptr, nullptr, 0},
    {"xer.fx.neon_edges", "Neon Edges", XER_PLUGIN_VIDEO_FX, fx::neon_edges, nullptr, nullptr, 0},
    {"xer.fx.halftone", "Halftone", XER_PLUGIN_VIDEO_FX, fx::halftone, nullptr, nullptr, 0},
    {"xer.fx.emboss_color", "Emboss Color", XER_PLUGIN_VIDEO_FX, fx::emboss_color, nullptr, nullptr, 0},
    {"xer.fx.edge_glow", "Edge Glow", XER_PLUGIN_VIDEO_FX, fx::edge_glow, nullptr, nullptr, 0},
    {"xer.fx.tone_split", "Tone Split", XER_PLUGIN_VIDEO_FX, fx::tone_split, nullptr, nullptr, 0},
};

static const int32_t kCount =
    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));

#include "../common/exports_impl.h"
