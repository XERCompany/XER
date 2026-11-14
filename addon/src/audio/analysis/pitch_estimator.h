#pragma once

// xer.audio.analysis.PitchEstimator — wraps XER::Audio::AnalysisAI::PitchEstimator.

#include <memory>

#include "audio/analysis_ai/anal_pitch_estimator.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class PitchEstWrap : public Nan::ObjectWrap {
public:
    explicit PitchEstWrap(int sample_rate);
    ~PitchEstWrap() override;

    XER::Audio::AnalysisAI::PitchEstimator& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Estimate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetLastPitch(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetLastHarmonics(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetLastNote(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::PitchEstimator a_;
};

PitchEstWrap* UnwrapPitchEstimator(v8::Local<v8::Value> v);
void InitAnalysisPitchEst(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
