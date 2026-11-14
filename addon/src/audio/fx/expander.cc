// xer.audio.fx.Expander — DynamicsExpander.

#include "expander.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::DynamicsExpander;

ExpanderWrap::ExpanderWrap() = default;
ExpanderWrap::~ExpanderWrap() = default;

ExpanderWrap* UnwrapExpander(v8::Local<v8::Value> v) {
    return UnwrapT<ExpanderWrap>(v);
}

void ExpanderWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ExpanderWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ExpanderWrap::SetThresholdDb(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapExpander(info.This());
    if (!w) return;
    w->Get().SetThresholdDb(float(NumArg(info, 0, -20.0)));
}

void ExpanderWrap::SetRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapExpander(info.This());
    if (!w) return;
    w->Get().SetRatio(float(NumArg(info, 0, 2.0)));
}

void ExpanderWrap::SetRangeDb(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapExpander(info.This());
    if (!w) return;
    w->Get().SetRangeDb(float(NumArg(info, 0, -60.0)));
}

void ExpanderWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapExpander(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Expander.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void ExpanderWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapExpander(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxExpander(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Expander", ExpanderWrap::New,
{         {"SetThresholdDb", ExpanderWrap::SetThresholdDb},
         {"SetRatio", ExpanderWrap::SetRatio},
         {"SetRangeDb", ExpanderWrap::SetRangeDb},
         {"ProcessBlock", ExpanderWrap::ProcessBlock},
         {"Process", ExpanderWrap::ProcessBlock},
         {"GetReport", ExpanderWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
