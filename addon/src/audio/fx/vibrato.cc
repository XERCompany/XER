// xer.audio.fx.Vibrato — ModVibrato.

#include "vibrato.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ModVibrato;

VibratoWrap::VibratoWrap() = default;
VibratoWrap::~VibratoWrap() = default;

VibratoWrap* UnwrapVibrato(v8::Local<v8::Value> v) {
    return UnwrapT<VibratoWrap>(v);
}

void VibratoWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new VibratoWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void VibratoWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVibrato(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(float(NumArg(info, 0, 44100.0)), size_t(IntArg(info, 1, 4410)))));
}

void VibratoWrap::SetRateHz(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVibrato(info.This());
    if (!w) return;
    w->Get().SetRateHz(float(NumArg(info, 0, 5.0)));
}

void VibratoWrap::SetDepthMs(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVibrato(info.This());
    if (!w) return;
    w->Get().SetDepthMs(float(NumArg(info, 0, 5.0)));
}

void VibratoWrap::SetMix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVibrato(info.This());
    if (!w) return;
    w->Get().SetMix(float(NumArg(info, 0, 1.0)));
}

void VibratoWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVibrato(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Vibrato.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void VibratoWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVibrato(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxVibrato(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Vibrato", VibratoWrap::New,
{         {"Initialize", VibratoWrap::Initialize},
         {"SetRateHz", VibratoWrap::SetRateHz},
         {"SetDepthMs", VibratoWrap::SetDepthMs},
         {"SetMix", VibratoWrap::SetMix},
         {"ProcessBlock", VibratoWrap::ProcessBlock},
         {"Process", VibratoWrap::ProcessBlock},
         {"GetReport", VibratoWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
