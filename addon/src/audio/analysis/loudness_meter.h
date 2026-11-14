#pragma once

// xer.audio.analysis.LoudnessMeter — wraps XER::Audio::AnalysisAI::LoudnessMeterLUFS.

#include <memory>

#include "audio/analysis_ai/anal_loudness_meter_lufs.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class LoudnessWrap : public Nan::ObjectWrap {
public:
    explicit LoudnessWrap();
    ~LoudnessWrap() override;

    XER::Audio::AnalysisAI::LoudnessMeterLUFS& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMomentaryLUFS(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetShortTermLUFS(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetIntegratedLUFS(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetTruePeakDBFS(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetProcessedBlocks(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetHistory(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::LoudnessMeterLUFS a_;
};

LoudnessWrap* UnwrapLoudnessMeterLUFS(v8::Local<v8::Value> v);
void InitAnalysisLoudness(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
