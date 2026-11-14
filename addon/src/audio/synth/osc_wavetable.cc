// xer.audio.synth.OscWavetable — SynthOscWavetable binding.

#include "osc_wavetable.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthOscWavetable;

OscWavetableWrap::OscWavetableWrap() = default;
OscWavetableWrap::~OscWavetableWrap() = default;

OscWavetableWrap* UnwrapOscWavetable(v8::Local<v8::Value> v) {
    return UnwrapT<OscWavetableWrap>(v);
}

void OscWavetableWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new OscWavetableWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void OscWavetableWrap::SetTable(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscWavetable(info.This());
    if (!w) return;
    std::vector<float> t;
    if (!F32FromJS(info[0], &t)) {
        Nan::ThrowError("OscWavetable.SetTable expects Float32Array");
        return;
    }
    w->Get().SetTable(t);
}

void OscWavetableWrap::BuildBandlimitedTable(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscWavetable(info.This());
    if (!w) return;
    w->Get().BuildBandlimitedTable();
}

void OscWavetableWrap::SetFrequency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscWavetable(info.This());
    if (!w) return;
    w->Get().SetFrequency(float(NumArg(info, 0, 440.0)));
}

void OscWavetableWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscWavetable(info.This());
    if (!w) { info.GetReturnValue().Set(Nan::New(0.0f)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().Process(
        static_cast<float>(NumArg(info, 0, 44100.0)))));
}


void InitSynthOscWavetable(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "OscWavetable", OscWavetableWrap::New,
        {{"SetTable", OscWavetableWrap::SetTable},
         {"BuildBandlimitedTable", OscWavetableWrap::BuildBandlimitedTable},
         {"SetFrequency", OscWavetableWrap::SetFrequency},
         {"Process", OscWavetableWrap::Process}});
}

} // namespace xer::addon::audio::synth
