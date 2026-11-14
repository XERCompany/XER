#pragma once

// xer.audio.synth.OscNoise — wraps XER::Audio::Synth::SynthOscNoise.

#include <memory>

#include "audio/synth_engine/synth_osc_noise.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class OscNoiseWrap : public Nan::ObjectWrap {
public:
    explicit OscNoiseWrap();
    ~OscNoiseWrap() override;

    XER::Audio::Synth::SynthOscNoise& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetSeed(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthOscNoise a_;
};

OscNoiseWrap* UnwrapOscNoise(v8::Local<v8::Value> v);
void InitSynthOscNoise(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
