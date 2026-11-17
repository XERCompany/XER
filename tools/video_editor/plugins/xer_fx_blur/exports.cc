#include "../common/entry.h"
#include "effects.h"

static const plugin::Entry kEntries[] = {
    {"xer.fx.directional_blur", "Directional Blur", XER_PLUGIN_VIDEO_FX, fx::directional_blur, nullptr, nullptr, 0},
    {"xer.fx.radial_blur", "Radial Blur", XER_PLUGIN_VIDEO_FX, fx::radial_blur, nullptr, nullptr, 0},
    {"xer.fx.zoom_blur", "Zoom Blur", XER_PLUGIN_VIDEO_FX, fx::zoom_blur, nullptr, nullptr, 0},
    {"xer.fx.fast_gaussian", "Fast Gaussian", XER_PLUGIN_VIDEO_FX, fx::fast_gaussian, nullptr, nullptr, 0},
    {"xer.fx.unsharp_mask", "Unsharp Mask", XER_PLUGIN_VIDEO_FX, fx::unsharp_mask, nullptr, nullptr, 0},
    {"xer.fx.median3", "Median 3x3", XER_PLUGIN_VIDEO_FX, fx::median3, nullptr, nullptr, 0},
};

static const int32_t kCount =
    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));

#include "../common/exports_impl.h"
