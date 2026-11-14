// xer.audio.dsp.STFT — short-time Fourier transform processor.

#include "stft.h"

#include <vector>

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::STFTProcessor;
using XER::Audio::DSP::STFTWindowMode;
using Complex = STFTProcessor::Complex;

STFTWrap::STFTWrap(size_t fft_size, size_t hop_size,
                   STFTWindowMode analysis_mode,
                   STFTWindowMode synthesis_mode)
    : stft_(fft_size, hop_size, analysis_mode, synthesis_mode) {}
STFTWrap::~STFTWrap() = default;

STFTWrap* UnwrapSTFT(v8::Local<v8::Value> v) { return UnwrapT<STFTWrap>(v); }

void STFTWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    const size_t fft = static_cast<size_t>(IntArg(info, 0, 1024));
    const size_t hop = static_cast<size_t>(IntArg(info, 1, 256));
    const auto am = EnumArg(info, 2, STFTWindowMode::kSqrtHann);
    const auto sm = EnumArg(info, 3, STFTWindowMode::kSqrtHann);
    auto* wrap = new STFTWrap(fft > 0 ? fft : 1024, hop > 0 ? hop : 256, am, sm);
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void STFTWrap::Analyze(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    if (!w || info.Length() < 1) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("STFT.Analyze expects Float32Array");
        return;
    }
    info.GetReturnValue().Set(
        Nan::New(w->Get().Analyze(in.data(), in.size())));
}

void STFTWrap::Synthesize(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    if (!w) return;
    const size_t n = info.Length() > 0 && info[0]->IsNumber()
        ? static_cast<size_t>(info[0].As<v8::Number>()->Value())
        : w->Get().GetFFTSize();
    std::vector<float> out(n, 0.0f);
    const bool ok = w->Get().Synthesize(out.data(), n);
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

namespace {

v8::Local<v8::Value> SpectrumToJS(const std::vector<Complex>& s) {
    std::vector<float> flat;
    flat.reserve(s.size() * 2);
    for (const auto& c : s) {
        flat.push_back(c.real());
        flat.push_back(c.imag());
    }
    return F32ToJS(flat);
}

} // namespace

void STFTWrap::GetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    if (!w) return;
    info.GetReturnValue().Set(SpectrumToJS(w->Get().GetSpectrum()));
}

void STFTWrap::SetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> flat;
    if (!F32FromJS(info[0], &flat)) return;
    auto& spec = w->Get().MutableSpectrum();
    const size_t n = flat.size() / 2;
    if (spec.size() != n) spec.resize(n);
    for (size_t i = 0; i < n; ++i) {
        spec[i] = Complex(flat[2 * i], flat[2 * i + 1]);
    }
}

void STFTWrap::RebuildHermitianSpectrum(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    if (w) w->Get().RebuildHermitianSpectrum();
}

void STFTWrap::GetFftSize(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetFFTSize()) : 0.0));
}

void STFTWrap::GetHopSize(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetHopSize()) : 0.0));
}

void STFTWrap::GetLatencySamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetLatencySamples()) : 0.0));
}

void STFTWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    if (w) w->Get().Reset();
}

void STFTWrap::GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSTFT(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspSTFT(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "STFT", STFTWrap::New,
        {{"Analyze", STFTWrap::Analyze},
         {"Synthesize", STFTWrap::Synthesize},
         {"GetSpectrum", STFTWrap::GetSpectrum},
         {"SetSpectrum", STFTWrap::SetSpectrum},
         {"RebuildHermitianSpectrum", STFTWrap::RebuildHermitianSpectrum},
         {"GetFftSize", STFTWrap::GetFftSize},
         {"GetHopSize", STFTWrap::GetHopSize},
         {"GetLatencySamples", STFTWrap::GetLatencySamples},
         {"Reset", STFTWrap::Reset},
         {"GetReport", STFTWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
