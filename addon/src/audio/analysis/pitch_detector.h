#pragma once

// xer.audio.analysis.PitchDetector — wraps XER::Audio::AnalysisAI::AudioPitchDetector.

#include <memory>

#include "audio/analysis_ai/audio_pitch_detector.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class PitchDetWrap : public Nan::ObjectWrap {
public:
    explicit PitchDetWrap(int sample_rate);
    ~PitchDetWrap() override;

    XER::Audio::AnalysisAI::AudioPitchDetector& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void DetectPitch(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsPitched(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFundamentalFrequency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetHarmonics(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void FrequencyToNote(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetConfidence(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSampleRate(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::AudioPitchDetector a_;
};

PitchDetWrap* UnwrapAudioPitchDetector(v8::Local<v8::Value> v);
void InitAnalysisPitchDet(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
