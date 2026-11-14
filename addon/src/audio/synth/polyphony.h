#pragma once

// xer.audio.synth.Polyphony — wraps XER::Audio::Synth::SynthPolyphonyController.

#include <memory>

#include "audio/synth_engine/synth_polyphony_controller.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class PolyphonyWrap : public Nan::ObjectWrap {
public:
    explicit PolyphonyWrap();
    ~PolyphonyWrap() override;

    XER::Audio::Synth::SynthPolyphonyController& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMaxVoices(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void AllocateVoice(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ReleaseVoice(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsAllocated(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAllocatedVoiceCount(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthPolyphonyController a_;
};

PolyphonyWrap* UnwrapPolyphony(v8::Local<v8::Value> v);
void InitSynthPolyphony(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
