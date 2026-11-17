#include "../common/entry.h"
#include "effects.h"

static const plugin::Entry kEntries[] = {
    {"xer.fx.film_grain", "Film Grain", XER_PLUGIN_VIDEO_FX, fx::film_grain, nullptr, nullptr, 0},
    {"xer.fx.letterbox", "Letterbox", XER_PLUGIN_VIDEO_FX, fx::letterbox, nullptr, nullptr, 0},
    {"xer.fx.vignette_soft", "Vignette Soft", XER_PLUGIN_VIDEO_FX, fx::vignette_soft, nullptr, nullptr, 0},
    {"xer.fx.grid_lines", "Grid Lines", XER_PLUGIN_VIDEO_FX, fx::grid_lines, nullptr, nullptr, 0},
    {"xer.fx.border_frame", "Border Frame", XER_PLUGIN_VIDEO_FX, fx::border_frame, nullptr, nullptr, 0},
    {"xer.fx.corner_marks", "Corner Marks", XER_PLUGIN_VIDEO_FX, fx::corner_marks, nullptr, nullptr, 0},
};

static const int32_t kCount =
    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));

#include "../common/exports_impl.h"
