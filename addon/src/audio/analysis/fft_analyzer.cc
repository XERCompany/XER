// xer.audio.analysis.FFTAnalyzer — AudioFFTAnalyzer binding.

#include "fft_analyzer.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::AudioFFTAnalyzer;

FftAnalyzerWrap::FftAnalyzerWrap(size_t fft_size) : a_(fft_size) {}
FftAnalyzerWrap::~FftAnalyzerWrap() = default;

FftAnalyzerWrap* UnwrapAudioFFTAnalyzer(v8::Local<v8::Value> v) {
    return UnwrapT<FftAnalyzerWrap>(v);
}

void FftAnalyzerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new FftAnalyzerWrap(IntArg(info, 0, 2048));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void FftAnalyzerWrap::ComputeFFT(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("FFTAnalyzer expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().ComputeFFT(
            in.data(), in.size())));
}

void FftAnalyzerWrap::GetFFTOutput(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    const auto& c = w->Get().GetFFTOutput();
        std::vector<float> flat;
        flat.reserve(c.size() * 2);
        for (const auto& z : c) { flat.push_back(z.real()); flat.push_back(z.imag()); }
        info.GetReturnValue().Set(F32ToJS(flat));
}

void FftAnalyzerWrap::GetMagnitudeSpectrum(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetMagnitudeSpectrum()));
}

void FftAnalyzerWrap::GetPhaseSpectrum(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetPhaseSpectrum()));
}

void FftAnalyzerWrap::AnalyzeSpectrum(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("FFTAnalyzer expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().AnalyzeSpectrum(
            in.data(), in.size())));
}

void FftAnalyzerWrap::GetDominantFrequency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetDominantFrequency(
            IntArg(info, 0, 44100))));
}

void FftAnalyzerWrap::GetFrequencyBands(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetFrequencyBands()));
}

void FftAnalyzerWrap::GetTotalPower(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetTotalPower()));
}

void FftAnalyzerWrap::GetRMSEnergy(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetRMSEnergy()));
}

void FftAnalyzerWrap::GetAnalysisReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioFFTAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetAnalysisReport()).ToLocalChecked());
}

void InitAnalysisFft(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "FFTAnalyzer", FftAnalyzerWrap::New,
        {{"ComputeFFT", FftAnalyzerWrap::ComputeFFT},
         {"GetFFTOutput", FftAnalyzerWrap::GetFFTOutput},
         {"GetMagnitudeSpectrum", FftAnalyzerWrap::GetMagnitudeSpectrum},
         {"GetPhaseSpectrum", FftAnalyzerWrap::GetPhaseSpectrum},
         {"AnalyzeSpectrum", FftAnalyzerWrap::AnalyzeSpectrum},
         {"GetDominantFrequency", FftAnalyzerWrap::GetDominantFrequency},
         {"GetFrequencyBands", FftAnalyzerWrap::GetFrequencyBands},
         {"GetTotalPower", FftAnalyzerWrap::GetTotalPower},
         {"GetRMSEnergy", FftAnalyzerWrap::GetRMSEnergy},
         {"GetAnalysisReport", FftAnalyzerWrap::GetAnalysisReport}});
}

} // namespace xer::addon::audio::analysis
