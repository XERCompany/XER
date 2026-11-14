#pragma once

// xer.audio.plugin.Vst3Scanner — XER::Audio::Plugin::ScanVst3Plugins.
// Free-function API -> plain functions on the plugin namespace.

#include "audio/audio_util.h"

namespace xer::addon::audio::plugin {

void InitPluginVst3Scanner(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
