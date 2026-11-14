// xer.audio.fx.Chorus — ModChorus.

#include "chorus.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ModChorus;

ChorusWrap::ChorusWrap() = default;
ChorusWrap::~ChorusWrap() = default;

ChorusWrap* UnwrapChorus(v8::Local<v8::Value> v) {
    return UnwrapT<ChorusWrap>(v);
}

void ChorusWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ChorusWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ChorusWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapChorus(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(float(NumArg(info, 0, 44100.0)), size_t(IntArg(info, 1, 4410)))));
}

void ChorusWrap::SetRateHz(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapChorus(info.This());
    if (!w) return;
    w->Get().SetRateHz(float(NumArg(info, 0, 1.0)));
}

void ChorusWrap::SetDepthSamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapChorus(info.This());
    if (!w) return;
    w->Get().SetDepthSamples(float(NumArg(info, 0, 20.0)));
}

void ChorusWrap::SetMix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapChorus(info.This());
    if (!w) return;
    w->Get().SetMix(float(NumArg(info, 0, 0.5)));
}

void ChorusWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapChorus(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Chorus.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void ChorusWrap::GetMemoryStats(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapChorus(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetMemoryStats() : std::string()).ToLocalChecked());
}

void ChorusWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapChorus(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxChorus(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Chorus", ChorusWrap::New,
{         {"Initialize", ChorusWrap::Initialize},
         {"SetRateHz", ChorusWrap::SetRateHz},
         {"SetDepthSamples", ChorusWrap::SetDepthSamples},
         {"SetMix", ChorusWrap::SetMix},
         {"ProcessBlock", ChorusWrap::ProcessBlock},
         {"Process", ChorusWrap::ProcessBlock},
         {"GetMemoryStats", ChorusWrap::GetMemoryStats},
         {"GetReport", ChorusWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
