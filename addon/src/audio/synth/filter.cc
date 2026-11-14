// xer.audio.synth.Filter — SynthFilterLadder binding.

#include "filter.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthFilterLadder;

FilterWrap::FilterWrap() = default;
FilterWrap::~FilterWrap() = default;

FilterWrap* UnwrapFilter(v8::Local<v8::Value> v) {
    return UnwrapT<FilterWrap>(v);
}

void FilterWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new FilterWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void FilterWrap::SetCutoff(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFilter(info.This());
    if (!w) return;
    w->Get().SetCutoff(float(NumArg(info, 0, 0.5)));
}

void FilterWrap::SetResonance(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFilter(info.This());
    if (!w) return;
    w->Get().SetResonance(float(NumArg(info, 0, 0.0)));
}

void FilterWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFilter(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Process(
        static_cast<float>(NumArg(info, 0, 0.0)))));
}


void InitSynthFilter(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Filter", FilterWrap::New,
        {{"SetCutoff", FilterWrap::SetCutoff},
         {"SetResonance", FilterWrap::SetResonance},
         {"Process", FilterWrap::Process}});
}

} // namespace xer::addon::audio::synth
