#pragma once

// xer.audio.dsp.Windowing — static window generators
// (XER::Audio::DSP::WindowingFunctions). Exposed as plain functions on the
// dsp namespace, not a class.

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

void InitDspWindowing(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
