// xer.audio.dsp.Convolution — convolution engine (reverb/IR processing).

#include "convolution.h"

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::ConvolutionEngine;

ConvolutionWrap::ConvolutionWrap() = default;
ConvolutionWrap::~ConvolutionWrap() = default;

ConvolutionWrap* UnwrapConvolution(v8::Local<v8::Value> v) {
    return UnwrapT<ConvolutionWrap>(v);
}

void ConvolutionWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ConvolutionWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ConvolutionWrap::SetImpulseResponse(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapConvolution(info.This());
    if (!w || info.Length() < 1) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    std::vector<float> ir;
    if (!F32FromJS(info[0], &ir)) {
        Nan::ThrowError("Convolution.SetImpulseResponse expects Float32Array");
        return;
    }
    info.GetReturnValue().Set(Nan::New(
        w->Get().SetImpulseResponse(ir.data(), ir.size())));
}

void ConvolutionWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapConvolution(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Convolution.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok = w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void ConvolutionWrap::GetImpulseSize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapConvolution(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetImpulseSize()) : 0.0));
}

void ConvolutionWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapConvolution(info.This());
    if (w) w->Get().Reset();
}

void ConvolutionWrap::GetMemoryStats(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapConvolution(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetMemoryStats() : std::string()).ToLocalChecked());
}

void ConvolutionWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapConvolution(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspConvolution(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Convolution", ConvolutionWrap::New,
        {{"SetImpulseResponse", ConvolutionWrap::SetImpulseResponse},
         {"ProcessBlock", ConvolutionWrap::ProcessBlock},
         {"Process", ConvolutionWrap::ProcessBlock},
         {"GetImpulseSize", ConvolutionWrap::GetImpulseSize},
         {"Reset", ConvolutionWrap::Reset},
         {"GetMemoryStats", ConvolutionWrap::GetMemoryStats},
         {"GetReport", ConvolutionWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
