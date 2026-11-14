#pragma once

// xer.audio.analysis.BeatTracker — wraps XER::Audio::AnalysisAI::BeatTracker.

#include <memory>

#include "audio/analysis_ai/anal_beat_tracker.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class BeatTrackerWrap : public Nan::ObjectWrap {
public:
    explicit BeatTrackerWrap(size_t window_size, size_t hop_size);
    ~BeatTrackerWrap() override;

    XER::Audio::AnalysisAI::BeatTracker& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Analyze(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBeatEvents(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetEstimatedBPM(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAverageStrength(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::BeatTracker a_;
};

BeatTrackerWrap* UnwrapBeatTracker(v8::Local<v8::Value> v);
void InitAnalysisBeatTracker(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
