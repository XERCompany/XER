// xer.audio.synth.Adsr — SynthEnvAdsr binding.

#include "adsr.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthEnvAdsr;

AdsrWrap::AdsrWrap() = default;
AdsrWrap::~AdsrWrap() = default;

AdsrWrap* UnwrapAdsr(v8::Local<v8::Value> v) {
    return UnwrapT<AdsrWrap>(v);
}

void AdsrWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new AdsrWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void AdsrWrap::Set(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAdsr(info.This());
    if (!w) return;
    w->Get().Set(NumArg(info,0,0.01), NumArg(info,1,0.1),
                      NumArg(info,2,0.7), NumArg(info,3,0.3));
}

void AdsrWrap::NoteOn(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAdsr(info.This());
    if (!w) return;
    w->Get().NoteOn();
}

void AdsrWrap::NoteOff(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAdsr(info.This());
    if (!w) return;
    w->Get().NoteOff();
}

void AdsrWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAdsr(info.This());
    if (!w) { info.GetReturnValue().Set(Nan::New(0.0f)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().Process(
        static_cast<float>(NumArg(info, 0, 44100.0)))));
}


void InitSynthAdsr(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Adsr", AdsrWrap::New,
        {{"Set", AdsrWrap::Set},
         {"NoteOn", AdsrWrap::NoteOn},
         {"NoteOff", AdsrWrap::NoteOff},
         {"Process", AdsrWrap::Process}});
}

} // namespace xer::addon::audio::synth
