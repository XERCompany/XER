#pragma once

// xer.audio.analysis.FFTAnalyzer — wraps XER::Audio::AnalysisAI::AudioFFTAnalyzer.

#include <memory>

#include "audio/analysis_ai/audio_fft_analyzer.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class FftAnalyzerWrap : public Nan::ObjectWrap {
public:
    explicit FftAnalyzerWrap(size_t fft_size);
    ~FftAnalyzerWrap() override;

    XER::Audio::AnalysisAI::AudioFFTAnalyzer& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ComputeFFT(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFFTOutput(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMagnitudeSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPhaseSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void AnalyzeSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDominantFrequency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFrequencyBands(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetTotalPower(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetRMSEnergy(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAnalysisReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::AudioFFTAnalyzer a_;
};

FftAnalyzerWrap* UnwrapAudioFFTAnalyzer(v8::Local<v8::Value> v);
void InitAnalysisFft(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
