#pragma once

// xer.audio.analysis.OnsetDetector — wraps XER::Audio::AnalysisAI::OnsetDetector.

#include <memory>

#include "audio/analysis_ai/anal_onset_detection.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class OnsetWrap : public Nan::ObjectWrap {
public:
    explicit OnsetWrap(size_t window_size, size_t hop_size);
    ~OnsetWrap() override;

    XER::Audio::AnalysisAI::OnsetDetector& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void DetectOnsets(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetOnsetFrames(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetOnsetTimesSeconds(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFluxCurve(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAverageStrength(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::OnsetDetector a_;
};

OnsetWrap* UnwrapOnsetDetector(v8::Local<v8::Value> v);
void InitAnalysisOnset(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
