#pragma once

// xer.audio.core.SourceLoader — wraps XER::Audio::Core::AudioSourceLoader.
// Decode entry point returning xer.audio.Buffer instances.

#include <memory>

#include "audio/audio_core/audio_source_loader.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class SourceLoaderWrap : public Nan::ObjectWrap {
public:
    explicit SourceLoaderWrap();
    ~SourceLoaderWrap() override;

    XER::Audio::Core::AudioSourceLoader& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    // Load({path, targetSampleRate?, targetChannels?, resampleQuality?,
    //       strictMp3?}) -> Buffer
    static void Load(const Nan::FunctionCallbackInfo<v8::Value>& info);
    // LoadOptions(path) -> plain options object (inspect defaults).
    static void DescribeOptions(
        const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioSourceLoader a_;
};

SourceLoaderWrap* UnwrapSourceLoader(v8::Local<v8::Value> v);
void InitCoreSourceLoader(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
