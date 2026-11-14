#pragma once

// xer.audio.synth.MultistageEnv — wraps XER::Audio::Synth::SynthEnvMultistage.

#include <memory>

#include "audio/synth_engine/synth_env_multistage.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class MultistageWrap : public Nan::ObjectWrap {
public:
    explicit MultistageWrap();
    ~MultistageWrap() override;

    XER::Audio::Synth::SynthEnvMultistage& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetStages(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Trigger(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthEnvMultistage a_;
};

MultistageWrap* UnwrapMultistage(v8::Local<v8::Value> v);
void InitSynthMultistage(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
