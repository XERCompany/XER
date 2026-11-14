// xer.audio.analysis.PitchDetector — AudioPitchDetector binding.

#include "pitch_detector.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::AudioPitchDetector;

PitchDetWrap::PitchDetWrap(int sample_rate) : a_(sample_rate) {}
PitchDetWrap::~PitchDetWrap() = default;

PitchDetWrap* UnwrapAudioPitchDetector(v8::Local<v8::Value> v) {
    return UnwrapT<PitchDetWrap>(v);
}

void PitchDetWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new PitchDetWrap(IntArg(info, 0, 44100));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void PitchDetWrap::DetectPitch(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioPitchDetector(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("PitchDetector.DetectPitch expects Float32Array");
            return;
        }
        const auto p = w->Get().DetectPitch(in.data(), in.size());
        v8::Local<v8::Object> o = Nan::New<v8::Object>();
        Nan::Set(o, Nan::New("frequency").ToLocalChecked(), Nan::New(p.frequency));
        Nan::Set(o, Nan::New("confidence").ToLocalChecked(), Nan::New(p.confidence));
        Nan::Set(o, Nan::New("semitoneOffset").ToLocalChecked(), Nan::New(p.semitone_offset));
        info.GetReturnValue().Set(o);
}

void PitchDetWrap::IsPitched(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioPitchDetector(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("PitchDetector.IsPitched expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().IsPitched(
            in.data(), in.size())));
}

void PitchDetWrap::GetFundamentalFrequency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioPitchDetector(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("PitchDetector.GetFundamentalFrequency expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().GetFundamentalFrequency(
            in.data(), in.size())));
}

void PitchDetWrap::GetHarmonics(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioPitchDetector(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("PitchDetector.GetHarmonics expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(F32ToJS(w->Get().GetHarmonics(
            in.data(), in.size())));
}

void PitchDetWrap::FrequencyToNote(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioPitchDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().FrequencyToNote(
            static_cast<float>(NumArg(info, 0)))).ToLocalChecked());
}

void PitchDetWrap::GetConfidence(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioPitchDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetConfidence()));
}

void PitchDetWrap::GetSampleRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAudioPitchDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetSampleRate()));
}

void InitAnalysisPitchDet(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "PitchDetector", PitchDetWrap::New,
        {{"DetectPitch", PitchDetWrap::DetectPitch},
         {"IsPitched", PitchDetWrap::IsPitched},
         {"GetFundamentalFrequency", PitchDetWrap::GetFundamentalFrequency},
         {"GetHarmonics", PitchDetWrap::GetHarmonics},
         {"FrequencyToNote", PitchDetWrap::FrequencyToNote},
         {"GetConfidence", PitchDetWrap::GetConfidence},
         {"GetSampleRate", PitchDetWrap::GetSampleRate}});
}

} // namespace xer::addon::audio::analysis
