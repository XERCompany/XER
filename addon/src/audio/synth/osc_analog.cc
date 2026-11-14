// xer.audio.synth.OscAnalog — SynthOscAnalog binding.

#include "osc_analog.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthOscAnalog;

OscAnalogWrap::OscAnalogWrap() = default;
OscAnalogWrap::~OscAnalogWrap() = default;

OscAnalogWrap* UnwrapOscAnalog(v8::Local<v8::Value> v) {
    return UnwrapT<OscAnalogWrap>(v);
}

void OscAnalogWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new OscAnalogWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void OscAnalogWrap::SetFrequency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscAnalog(info.This());
    if (!w) return;
    w->Get().SetFrequency(float(NumArg(info, 0, 440.0)));
}

void OscAnalogWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOscAnalog(info.This());
    if (!w) { info.GetReturnValue().Set(Nan::New(0.0f)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().Process(
        static_cast<float>(NumArg(info, 0, 44100.0)))));
}


void InitSynthOscAnalog(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "OscAnalog", OscAnalogWrap::New,
        {{"SetFrequency", OscAnalogWrap::SetFrequency},
         {"Process", OscAnalogWrap::Process}});
}

} // namespace xer::addon::audio::synth
