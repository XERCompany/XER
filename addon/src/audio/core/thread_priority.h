#pragma once

// xer.audio.core.ThreadPriority — XER::Audio::Core::AudioThreadPriority.
// All-native-static API -> exposed as plain functions on the namespace.

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

// ThreadPriority enum ordinals: LOW=0, NORMAL=1, HIGH=2, CRITICAL=3.
void InitCoreThreadPriority(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
