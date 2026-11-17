#include "../common/entry.h"
#include "effects.h"

static const plugin::Entry kEntries[] = {
    {"xer.af.echo", "Echo", XER_PLUGIN_AUDIO_FX, nullptr, fx::echo, nullptr, 0},
    {"xer.af.tremolo", "Tremolo", XER_PLUGIN_AUDIO_FX, nullptr, fx::tremolo, nullptr, 0},
    {"xer.af.autopan", "Auto Pan", XER_PLUGIN_AUDIO_FX, nullptr, fx::autopan, nullptr, 0},
    {"xer.af.bitcrusher", "Bitcrusher", XER_PLUGIN_AUDIO_FX, nullptr, fx::bitcrusher, nullptr, 0},
    {"xer.af.onepole_lp", "Lowpass", XER_PLUGIN_AUDIO_FX, nullptr, fx::onepole_lp, nullptr, 0},
    {"xer.af.soft_clip", "Soft Clip", XER_PLUGIN_AUDIO_FX, nullptr, fx::soft_clip, nullptr, 0},
};

static const int32_t kCount =
    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));

#include "../common/exports_impl.h"
