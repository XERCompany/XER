// xer.audio.synth.OscNoise — SynthOscNoise binding.

#include "osc_noise.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthOscNoise;

OscNoiseWrap::OscNoiseWrap() = default;
OscNoiseWrap::~OscNoiseWrap() = default;

OscNoiseWrap* UnwrapOscNoise(v8::Local<v8::Value> v) {
    return UnwrapT<OscNoiseWrap>(v);
}

void OscNoiseWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new OscNoiseWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void OscNoiseWrap::SetSeed(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscNoise(info.This());
    if (!w) return;
    w->Get().SetSeed(uint32_t(IntArg(info, 0, 1)));
}

void OscNoiseWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscNoise(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Process()));
}


void InitSynthOscNoise(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "OscNoise", OscNoiseWrap::New,
        {{"SetSeed", OscNoiseWrap::SetSeed},
         {"Process", OscNoiseWrap::Process}});
}

} // namespace xer::addon::audio::synth
