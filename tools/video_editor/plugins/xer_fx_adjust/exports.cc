// ABI table — binds effects.h prototypes to the plugin exports.
#include "../common/entry.h"
#include "effects.h"

static const plugin::Entry kEntries[] = {
    {"xer.fx.exposure", "Exposure", XER_PLUGIN_VIDEO_FX, fx::exposure, nullptr, nullptr, 0},
    {"xer.fx.gamma", "Gamma", XER_PLUGIN_VIDEO_FX, fx::gamma_corr, nullptr, nullptr, 0},
    {"xer.fx.levels", "Levels", XER_PLUGIN_VIDEO_FX, fx::levels, nullptr, nullptr, 0},
    {"xer.fx.auto_contrast", "Auto Contrast", XER_PLUGIN_VIDEO_FX, fx::auto_contrast, nullptr, nullptr, 0},
    {"xer.fx.temperature", "Temperature", XER_PLUGIN_VIDEO_FX, fx::temperature, nullptr, nullptr, 0},
    {"xer.fx.tint", "Tint", XER_PLUGIN_VIDEO_FX, fx::tint, nullptr, nullptr, 0},
    {"xer.fx.vibrance", "Vibrance", XER_PLUGIN_VIDEO_FX, fx::vibrance, nullptr, nullptr, 0},
    {"xer.fx.lift_gamma_gain", "Lift/Gamma/Gain", XER_PLUGIN_VIDEO_FX, fx::lift_gamma_gain, nullptr, nullptr, 0},
};

static const int32_t kCount =
    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));

#include "../common/exports_impl.h"
