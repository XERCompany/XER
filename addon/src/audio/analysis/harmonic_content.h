#pragma once

// xer.audio.analysis.HarmonicContent — wraps XER::Audio::AnalysisAI::HarmonicContentAnalyzer.

#include <memory>

#include "audio/analysis_ai/anal_harmonic_content.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class HarmonicWrap : public Nan::ObjectWrap {
public:
    explicit HarmonicWrap(size_t fft_size);
    ~HarmonicWrap() override;

    XER::Audio::AnalysisAI::HarmonicContentAnalyzer& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Analyze(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFundamentalFrequency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetHarmonicRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetInharmonicity(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetHarmonicSeries(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSummary(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::HarmonicContentAnalyzer a_;
};

HarmonicWrap* UnwrapHarmonicContentAnalyzer(v8::Local<v8::Value> v);
void InitAnalysisHarmonic(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
