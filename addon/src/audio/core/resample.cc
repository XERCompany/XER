// xer.audio.core.Resample — AudioSampleRateConverter binding.

#include "resample.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioSampleRateConverter;

ResampleWrap::ResampleWrap(int quality)
    : a_(static_cast<XER::Audio::Core::ResampleQuality>(quality)) {}
ResampleWrap::~ResampleWrap() = default;

ResampleWrap* UnwrapResample(v8::Local<v8::Value> v) {
    return UnwrapT<ResampleWrap>(v);
}

void ResampleWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ResampleWrap(IntArg(info, 0, 2));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ResampleWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        IntArg(info, 0, 44100), IntArg(info, 1, 48000))));
}

void ResampleWrap::IsInitialized(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsInitialized()));
}

void ResampleWrap::Convert(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Resample.Convert expects Float32Array");
        return;
    }
    const double ratio = w->Get().GetRatio();
    std::vector<float> out(static_cast<size_t>(
        in.size() * ratio) + 64, 0.0f);
    size_t produced = out.size();
    if (!w->Get().Convert(in.data(), in.size(), out.data(), produced))
        return;
    out.resize(produced);
    info.GetReturnValue().Set(F32ToJS(out));
}

void ResampleWrap::GetRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetRatio()));
}

void ResampleWrap::GetInputRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetInputRate()));
}

void ResampleWrap::GetOutputRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetOutputRate()));
}

void ResampleWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    if (!w) return;
    w->Get().Reset();
}

void ResampleWrap::GetInfo(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapResample(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetInfo()).ToLocalChecked());
}


void InitCoreSampleRate(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Resample", ResampleWrap::New,
        {{"Initialize", ResampleWrap::Initialize},
         {"IsInitialized", ResampleWrap::IsInitialized},
         {"Convert", ResampleWrap::Convert},
         {"GetRatio", ResampleWrap::GetRatio},
         {"GetInputRate", ResampleWrap::GetInputRate},
         {"GetOutputRate", ResampleWrap::GetOutputRate},
         {"Reset", ResampleWrap::Reset},
         {"GetInfo", ResampleWrap::GetInfo},
         });
}

} // namespace xer::addon::audio::core
