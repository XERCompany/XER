#pragma once

// xer.audio.core.Interleave — wraps XER::Audio::Core::AudioInterleaveProcessor.

#include <memory>

#include "audio/audio_core/audio_interleave_processor.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class InterleaveWrap : public Nan::ObjectWrap {
public:
    explicit InterleaveWrap();
    ~InterleaveWrap() override;

    XER::Audio::Core::AudioInterleaveProcessor& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Interleave(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Deinterleave(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioInterleaveProcessor a_;};

InterleaveWrap* UnwrapInterleave(v8::Local<v8::Value> v);
void InitCoreInterleave(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
