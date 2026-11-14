// xer.audio.synth.Lfo — SynthLfoStandard binding.

#include "lfo.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthLfoStandard;

LfoWrap::LfoWrap() = default;
LfoWrap::~LfoWrap() = default;

LfoWrap* UnwrapLfo(v8::Local<v8::Value> v) {
    return UnwrapT<LfoWrap>(v);
}

void LfoWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new LfoWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void LfoWrap::SetRateHz(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLfo(info.This());
    if (!w) return;
    w->Get().SetRateHz(float(NumArg(info, 0, 1.0)));
}

void LfoWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLfo(info.This());
    if (!w) { info.GetReturnValue().Set(Nan::New(0.0f)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().Process(
        static_cast<float>(NumArg(info, 0, 44100.0)))));
}


void InitSynthLfo(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Lfo", LfoWrap::New,
        {{"SetRateHz", LfoWrap::SetRateHz},
         {"Process", LfoWrap::Process}});
}

} // namespace xer::addon::audio::synth
