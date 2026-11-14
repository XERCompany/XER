// xer.audio.dsp.SpectralShaper — spectral magnitude shaping.

#include "spectralshaper.h"

#include <vector>

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::SpectralShaper;

SpectralShaperWrap::SpectralShaperWrap(size_t fft_size, size_t hop_size)
    : shaper_(fft_size, hop_size) {}
SpectralShaperWrap::~SpectralShaperWrap() = default;

SpectralShaperWrap* UnwrapSpectralShaper(v8::Local<v8::Value> v) {
    return UnwrapT<SpectralShaperWrap>(v);
}

void SpectralShaperWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    const size_t fft = static_cast<size_t>(IntArg(info, 0, 1024));
    const size_t hop = static_cast<size_t>(IntArg(info, 1, 256));
    auto* wrap = new SpectralShaperWrap(fft > 0 ? fft : 1024,
                                       hop > 0 ? hop : 256);
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void SpectralShaperWrap::SetCurve(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectralShaper(info.This());
    if (!w || info.Length() < 1) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    std::vector<float> curve;
    if (!F32FromJS(info[0], &curve)) {
        Nan::ThrowError("SpectralShaper.SetCurve expects Float32Array");
        return;
    }
    info.GetReturnValue().Set(Nan::New(w->Get().SetCurve(curve)));
}

void SpectralShaperWrap::GetCurve(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectralShaper(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetCurve()));
}

void SpectralShaperWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectralShaper(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("SpectralShaper.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok =
        w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void SpectralShaperWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    SpectralShaperWrap::ProcessBlock(info);
}

void SpectralShaperWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectralShaper(info.This());
    if (w) w->Get().Reset();
}

void SpectralShaperWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectralShaper(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspSpectralShaper(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "SpectralShaper", SpectralShaperWrap::New,
        {{"SetCurve", SpectralShaperWrap::SetCurve},
         {"GetCurve", SpectralShaperWrap::GetCurve},
         {"ProcessBlock", SpectralShaperWrap::ProcessBlock},
         {"Process", SpectralShaperWrap::Process},
         {"Reset", SpectralShaperWrap::Reset},
         {"GetReport", SpectralShaperWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
