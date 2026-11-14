#pragma once

// xer.audio.analysis.RmsPeak — wraps XER::Audio::AnalysisAI::RMSPeakDetector.

#include <memory>

#include "audio/analysis_ai/anal_rms_peak_detector.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class RmsPeakWrap : public Nan::ObjectWrap {
public:
    explicit RmsPeakWrap();
    ~RmsPeakWrap() override;

    XER::Audio::AnalysisAI::RMSPeakDetector& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Analyze(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetRMS(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPeak(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCrestFactor(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPeakDBFS(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetWindowHistory(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::RMSPeakDetector a_;
};

RmsPeakWrap* UnwrapRMSPeakDetector(v8::Local<v8::Value> v);
void InitAnalysisRmsPeak(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
