// xer.audio.dsp.FFT — real FFT engine.
// Spectrum is exposed as a Float32Array of interleaved [re, im, ...] pairs.

#include "fft.h"

#include <vector>

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::FFTEngine;

FFTWrap::FFTWrap(size_t fft_size) : eng_(fft_size) {}
FFTWrap::~FFTWrap() = default;

FFTWrap* UnwrapFFT(v8::Local<v8::Value> v) { return UnwrapT<FFTWrap>(v); }

void FFTWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    const size_t n = static_cast<size_t>(IntArg(info, 0, 1024));
    auto* wrap = new FFTWrap(n > 0 ? n : 1024);
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void FFTWrap::Forward(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    if (!w || info.Length() < 1) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("FFT.Forward expects a Float32Array");
        return;
    }
    info.GetReturnValue().Set(
        Nan::New(w->Get().Forward(in.data(), in.size())));
}

void FFTWrap::Inverse(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    if (!w) return;
    // spectrum_ holds fft_size complex bins; Inverse emits fft_size floats.
    const size_t n = w->Get().GetSpectrum().size();
    std::vector<float> out(n);
    const bool ok = w->Get().Inverse(out.data(), n);
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

namespace {

v8::Local<v8::Value> SpectrumToJS(const std::vector<FFTEngine::Complex>& s) {
    std::vector<float> flat;
    flat.reserve(s.size() * 2);
    for (const auto& c : s) {
        flat.push_back(c.real());
        flat.push_back(c.imag());
    }
    return F32ToJS(flat);
}

} // namespace

void FFTWrap::GetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    if (!w) return;
    info.GetReturnValue().Set(SpectrumToJS(w->Get().GetSpectrum()));
}

void FFTWrap::SetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> flat;
    if (!F32FromJS(info[0], &flat)) return;
    auto& spec = w->Get().MutableSpectrum();
    const size_t n = flat.size() / 2;
    if (spec.size() != n) spec.resize(n);
    for (size_t i = 0; i < n; ++i) {
        spec[i] = FFTEngine::Complex(flat[2 * i], flat[2 * i + 1]);
    }
}

void FFTWrap::GetMagnitude(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetMagnitude()));
}

void FFTWrap::GetFftSize(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    info.GetReturnValue().Set(
        Nan::New(w ? static_cast<double>(w->Get().GetSpectrum().size())
                   : 0.0));
}

void FFTWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    if (w) w->Get().Reset();
}

void FFTWrap::GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFFT(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspFFT(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "FFT", FFTWrap::New,
        {{"Forward", FFTWrap::Forward},
         {"Inverse", FFTWrap::Inverse},
         {"GetSpectrum", FFTWrap::GetSpectrum},
         {"SetSpectrum", FFTWrap::SetSpectrum},
         {"GetMagnitude", FFTWrap::GetMagnitude},
         {"GetFftSize", FFTWrap::GetFftSize},
         {"Reset", FFTWrap::Reset},
         {"GetReport", FFTWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
