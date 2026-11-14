#pragma once

// xer.audio.fx.custom — custom effect package runner.
// Free-function API over CustomEffectPackage/Node/Flow structs.

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

void InitFxCustom(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
