// xer.audio.fx.ReverbAlgo — ReverbAlgorithmic.

#include "reverb_algo.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ReverbAlgorithmic;

ReverbAlgoWrap::ReverbAlgoWrap() = default;
ReverbAlgoWrap::~ReverbAlgoWrap() = default;

ReverbAlgoWrap* UnwrapReverbAlgo(v8::Local<v8::Value> v) {
    return UnwrapT<ReverbAlgoWrap>(v);
}

void ReverbAlgoWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ReverbAlgoWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ReverbAlgoWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbAlgo(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(float(NumArg(info, 0, 44100.0)), size_t(IntArg(info, 1, 44100)))));
}

void ReverbAlgoWrap::SetRoomSize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbAlgo(info.This());
    if (!w) return;
    w->Get().SetRoomSize(float(NumArg(info, 0, 0.5)));
}

void ReverbAlgoWrap::SetDamping(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbAlgo(info.This());
    if (!w) return;
    w->Get().SetDamping(float(NumArg(info, 0, 0.5)));
}

void ReverbAlgoWrap::SetMix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbAlgo(info.This());
    if (!w) return;
    w->Get().SetMix(float(NumArg(info, 0, 0.33)));
}

void ReverbAlgoWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbAlgo(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("ReverbAlgo.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void ReverbAlgoWrap::GetMemoryStats(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbAlgo(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetMemoryStats() : std::string()).ToLocalChecked());
}

void ReverbAlgoWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbAlgo(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxReverbAlgo(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ReverbAlgo", ReverbAlgoWrap::New,
{         {"Initialize", ReverbAlgoWrap::Initialize},
         {"SetRoomSize", ReverbAlgoWrap::SetRoomSize},
         {"SetDamping", ReverbAlgoWrap::SetDamping},
         {"SetMix", ReverbAlgoWrap::SetMix},
         {"ProcessBlock", ReverbAlgoWrap::ProcessBlock},
         {"Process", ReverbAlgoWrap::ProcessBlock},
         {"GetMemoryStats", ReverbAlgoWrap::GetMemoryStats},
         {"GetReport", ReverbAlgoWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
