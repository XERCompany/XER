// xer.audio.fx.Flanger — ModFlanger.

#include "flanger.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ModFlanger;

FlangerWrap::FlangerWrap() = default;
FlangerWrap::~FlangerWrap() = default;

FlangerWrap* UnwrapFlanger(v8::Local<v8::Value> v) {
    return UnwrapT<FlangerWrap>(v);
}

void FlangerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new FlangerWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void FlangerWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(float(NumArg(info, 0, 44100.0)), size_t(IntArg(info, 1, 4410)))));
}

void FlangerWrap::SetRateHz(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    if (!w) return;
    w->Get().SetRateHz(float(NumArg(info, 0, 0.5)));
}

void FlangerWrap::SetDepthSamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    if (!w) return;
    w->Get().SetDepthSamples(float(NumArg(info, 0, 5.0)));
}

void FlangerWrap::SetFeedback(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    if (!w) return;
    w->Get().SetFeedback(float(NumArg(info, 0, 0.5)));
}

void FlangerWrap::SetMix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    if (!w) return;
    w->Get().SetMix(float(NumArg(info, 0, 0.5)));
}

void FlangerWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Flanger.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void FlangerWrap::GetMemoryStats(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetMemoryStats() : std::string()).ToLocalChecked());
}

void FlangerWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlanger(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxFlanger(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Flanger", FlangerWrap::New,
{         {"Initialize", FlangerWrap::Initialize},
         {"SetRateHz", FlangerWrap::SetRateHz},
         {"SetDepthSamples", FlangerWrap::SetDepthSamples},
         {"SetFeedback", FlangerWrap::SetFeedback},
         {"SetMix", FlangerWrap::SetMix},
         {"ProcessBlock", FlangerWrap::ProcessBlock},
         {"Process", FlangerWrap::ProcessBlock},
         {"GetMemoryStats", FlangerWrap::GetMemoryStats},
         {"GetReport", FlangerWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
