// xer.audio.fx.ReverbConv — convolution reverb with impulse response.

#include "reverb_conv.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ReverbConvolution;

ReverbConvWrap::ReverbConvWrap() = default;
ReverbConvWrap::~ReverbConvWrap() = default;

ReverbConvWrap* UnwrapReverbConv(v8::Local<v8::Value> v) {
    return UnwrapT<ReverbConvWrap>(v);
}

void ReverbConvWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ReverbConvWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ReverbConvWrap::LoadImpulseResponse(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbConv(info.This());
    if (!w || info.Length() < 1) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    std::vector<float> ir;
    if (!F32FromJS(info[0], &ir)) {
        Nan::ThrowError("ReverbConv.LoadImpulseResponse expects Float32Array");
        return;
    }
    info.GetReturnValue().Set(Nan::New(
        w->Get().LoadImpulseResponse(ir.data(), ir.size())));
}

void ReverbConvWrap::SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbConv(info.This());
    if (w) w->Get().SetMix(static_cast<float>(NumArg(info, 0, 0.33)));
}

void ReverbConvWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbConv(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("ReverbConv.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok = w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void ReverbConvWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    ReverbConvWrap::ProcessBlock(info);
}

void ReverbConvWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapReverbConv(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxReverbConv(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ReverbConv", ReverbConvWrap::New,
        {{"LoadImpulseResponse", ReverbConvWrap::LoadImpulseResponse},
         {"SetMix", ReverbConvWrap::SetMix},
         {"ProcessBlock", ReverbConvWrap::ProcessBlock},
         {"Process", ReverbConvWrap::Process},
         {"GetReport", ReverbConvWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
