// xer.audio.synth.Polyphony — SynthPolyphonyController binding.

#include "polyphony.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthPolyphonyController;

PolyphonyWrap::PolyphonyWrap() = default;
PolyphonyWrap::~PolyphonyWrap() = default;

PolyphonyWrap* UnwrapPolyphony(v8::Local<v8::Value> v) {
    return UnwrapT<PolyphonyWrap>(v);
}

void PolyphonyWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new PolyphonyWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void PolyphonyWrap::SetMaxVoices(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPolyphony(info.This());
    if (!w) return;
    w->Get().SetMaxVoices(uint32_t(IntArg(info, 0, 16)));
}

void PolyphonyWrap::AllocateVoice(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPolyphony(info.This());
    if (!w) return;
    uint8_t stolen = 0xFF;
    const int v = w->Get().AllocateVoice(
        static_cast<uint8_t>(IntArg(info, 0, 60)), &stolen);
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("voice").ToLocalChecked(), Nan::New(v));
    Nan::Set(o, Nan::New("stolenNote").ToLocalChecked(),
             Nan::New(stolen == 0xFF ? -1 : static_cast<int>(stolen)));
    info.GetReturnValue().Set(o);
}

void PolyphonyWrap::ReleaseVoice(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPolyphony(info.This());
    if (!w) return;
    w->Get().ReleaseVoice(static_cast<uint8_t>(IntArg(info, 0, 0)));
}

void PolyphonyWrap::IsAllocated(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPolyphony(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsAllocated(static_cast<uint8_t>(IntArg(info, 0, 0)))));
}

void PolyphonyWrap::GetAllocatedVoiceCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPolyphony(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetAllocatedVoiceCount())));
}


void InitSynthPolyphony(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Polyphony", PolyphonyWrap::New,
        {{"SetMaxVoices", PolyphonyWrap::SetMaxVoices},
         {"AllocateVoice", PolyphonyWrap::AllocateVoice},
         {"ReleaseVoice", PolyphonyWrap::ReleaseVoice},
         {"IsAllocated", PolyphonyWrap::IsAllocated},
         {"GetAllocatedVoiceCount", PolyphonyWrap::GetAllocatedVoiceCount}});
}

} // namespace xer::addon::audio::synth
