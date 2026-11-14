// xer.audio.fx.Limiter — DynamicsLimiter.

#include "limiter.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::DynamicsLimiter;

LimiterWrap::LimiterWrap() = default;
LimiterWrap::~LimiterWrap() = default;

LimiterWrap* UnwrapLimiter(v8::Local<v8::Value> v) {
    return UnwrapT<LimiterWrap>(v);
}

void LimiterWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new LimiterWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void LimiterWrap::SetCeilingDb(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLimiter(info.This());
    if (!w) return;
    w->Get().SetCeilingDb(float(NumArg(info, 0, -1.0)));
}

void LimiterWrap::SetRelease(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLimiter(info.This());
    if (!w) return;
    w->Get().SetRelease(float(NumArg(info, 0, 0.1)));
}

void LimiterWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLimiter(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Limiter.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void LimiterWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLimiter(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxLimiter(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Limiter", LimiterWrap::New,
{         {"SetCeilingDb", LimiterWrap::SetCeilingDb},
         {"SetRelease", LimiterWrap::SetRelease},
         {"ProcessBlock", LimiterWrap::ProcessBlock},
         {"Process", LimiterWrap::ProcessBlock},
         {"GetReport", LimiterWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
