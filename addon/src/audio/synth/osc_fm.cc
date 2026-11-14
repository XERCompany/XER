// xer.audio.synth.OscFm — SynthOscFm binding.

#include "osc_fm.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthOscFm;

OscFmWrap::OscFmWrap() = default;
OscFmWrap::~OscFmWrap() = default;

OscFmWrap* UnwrapOscFm(v8::Local<v8::Value> v) {
    return UnwrapT<OscFmWrap>(v);
}

void OscFmWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new OscFmWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void OscFmWrap::SetCarrier(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscFm(info.This());
    if (!w) return;
    w->Get().SetCarrier(float(NumArg(info, 0, 440.0)));
}

void OscFmWrap::SetModulator(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscFm(info.This());
    if (!w) return;
    w->Get().SetModulator(float(NumArg(info, 0, 220.0)));
}

void OscFmWrap::SetIndex(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscFm(info.This());
    if (!w) return;
    w->Get().SetIndex(float(NumArg(info, 0, 0.0)));
}

void OscFmWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscFm(info.This());
    if (!w) { info.GetReturnValue().Set(Nan::New(0.0f)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().Process(
        static_cast<float>(NumArg(info, 0, 44100.0)))));
}


void InitSynthOscFm(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "OscFm", OscFmWrap::New,
        {{"SetCarrier", OscFmWrap::SetCarrier},
         {"SetModulator", OscFmWrap::SetModulator},
         {"SetIndex", OscFmWrap::SetIndex},
         {"Process", OscFmWrap::Process}});
}

} // namespace xer::addon::audio::synth
