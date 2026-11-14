// xer.audio.fx.RingMod — ModRingMod.

#include "ringmod.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ModRingMod;

RingModWrap::RingModWrap() = default;
RingModWrap::~RingModWrap() = default;

RingModWrap* UnwrapRingMod(v8::Local<v8::Value> v) {
    return UnwrapT<RingModWrap>(v);
}

void RingModWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new RingModWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void RingModWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRingMod(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(float(NumArg(info, 0, 44100.0)))));
}

void RingModWrap::SetFrequencyHz(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRingMod(info.This());
    if (!w) return;
    w->Get().SetFrequencyHz(float(NumArg(info, 0, 440.0)));
}

void RingModWrap::SetMix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRingMod(info.This());
    if (!w) return;
    w->Get().SetMix(float(NumArg(info, 0, 1.0)));
}

void RingModWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRingMod(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("RingMod.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void RingModWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRingMod(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxRingMod(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "RingMod", RingModWrap::New,
{         {"Initialize", RingModWrap::Initialize},
         {"SetFrequencyHz", RingModWrap::SetFrequencyHz},
         {"SetMix", RingModWrap::SetMix},
         {"ProcessBlock", RingModWrap::ProcessBlock},
         {"Process", RingModWrap::ProcessBlock},
         {"GetReport", RingModWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
