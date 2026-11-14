#pragma once

// xer.audio.synth.Adsr — wraps XER::Audio::Synth::SynthEnvAdsr.

#include <memory>

#include "audio/synth_engine/synth_env_adsr.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class AdsrWrap : public Nan::ObjectWrap {
public:
    explicit AdsrWrap();
    ~AdsrWrap() override;

    XER::Audio::Synth::SynthEnvAdsr& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Set(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void NoteOn(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void NoteOff(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthEnvAdsr a_;
};

AdsrWrap* UnwrapAdsr(v8::Local<v8::Value> v);
void InitSynthAdsr(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
