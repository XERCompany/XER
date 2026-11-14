// xer.audio.fx.Gate — DynamicsGate.

#include "gate.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::DynamicsGate;

GateWrap::GateWrap() = default;
GateWrap::~GateWrap() = default;

GateWrap* UnwrapGate(v8::Local<v8::Value> v) {
    return UnwrapT<GateWrap>(v);
}

void GateWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new GateWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void GateWrap::SetThresholdDb(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGate(info.This());
    if (!w) return;
    w->Get().SetThresholdDb(float(NumArg(info, 0, -40.0)));
}

void GateWrap::SetHoldSamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGate(info.This());
    if (!w) return;
    w->Get().SetHoldSamples(size_t(IntArg(info, 0, 4410)));
}

void GateWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGate(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Gate.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void GateWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGate(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxGate(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Gate", GateWrap::New,
{         {"SetThresholdDb", GateWrap::SetThresholdDb},
         {"SetHoldSamples", GateWrap::SetHoldSamples},
         {"ProcessBlock", GateWrap::ProcessBlock},
         {"Process", GateWrap::ProcessBlock},
         {"GetReport", GateWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
