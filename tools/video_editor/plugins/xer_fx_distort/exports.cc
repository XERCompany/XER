#include "../common/entry.h"
#include "effects.h"

static const plugin::Entry kEntries[] = {
    {"xer.fx.wave_h", "Wave Horizontal", XER_PLUGIN_VIDEO_FX, fx::wave_h, nullptr, nullptr, 0},
    {"xer.fx.wave_v", "Wave Vertical", XER_PLUGIN_VIDEO_FX, fx::wave_v, nullptr, nullptr, 0},
    {"xer.fx.ripple", "Ripple", XER_PLUGIN_VIDEO_FX, fx::ripple, nullptr, nullptr, 0},
    {"xer.fx.mirror", "Mirror", XER_PLUGIN_VIDEO_FX, fx::mirror_quad, nullptr, nullptr, 0},
    {"xer.fx.tile", "Tile", XER_PLUGIN_VIDEO_FX, fx::tile, nullptr, nullptr, 0},
    {"xer.fx.twirl", "Twirl", XER_PLUGIN_VIDEO_FX, fx::twirl, nullptr, nullptr, 0},
};

static const int32_t kCount =
    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));

#include "../common/exports_impl.h"
