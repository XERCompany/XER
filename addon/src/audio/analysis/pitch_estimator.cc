// xer.audio.analysis.PitchEstimator — PitchEstimator binding.

#include "pitch_estimator.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::PitchEstimator;

PitchEstWrap::PitchEstWrap(int sample_rate) : a_(sample_rate) {}
PitchEstWrap::~PitchEstWrap() = default;

PitchEstWrap* UnwrapPitchEstimator(v8::Local<v8::Value> v) {
    return UnwrapT<PitchEstWrap>(v);
}

void PitchEstWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new PitchEstWrap(IntArg(info, 0, 44100));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void PitchEstWrap::Estimate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchEstimator(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("PitchEstimator.Estimate expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().Estimate(
            in.data(), in.size())));
}

void PitchEstWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchEstimator(info.This());
    if (!w) return;
    w->Get().Reset();
}

void PitchEstWrap::GetLastPitch(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchEstimator(info.This());
    if (!w) return;
    const auto& p = w->Get().GetLastPitch();
        v8::Local<v8::Object> o = Nan::New<v8::Object>();
        Nan::Set(o, Nan::New("frequency").ToLocalChecked(), Nan::New(p.frequency));
        Nan::Set(o, Nan::New("confidence").ToLocalChecked(), Nan::New(p.confidence));
        Nan::Set(o, Nan::New("semitoneOffset").ToLocalChecked(), Nan::New(p.semitone_offset));
        info.GetReturnValue().Set(o);
}

void PitchEstWrap::GetLastHarmonics(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchEstimator(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetLastHarmonics()));
}

void PitchEstWrap::GetLastNote(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchEstimator(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetLastNote()).ToLocalChecked());
}

void PitchEstWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchEstimator(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetReport()).ToLocalChecked());
}

void InitAnalysisPitchEst(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "PitchEstimator", PitchEstWrap::New,
        {{"Estimate", PitchEstWrap::Estimate},
         {"Reset", PitchEstWrap::Reset},
         {"GetLastPitch", PitchEstWrap::GetLastPitch},
         {"GetLastHarmonics", PitchEstWrap::GetLastHarmonics},
         {"GetLastNote", PitchEstWrap::GetLastNote},
         {"GetReport", PitchEstWrap::GetReport}});
}

} // namespace xer::addon::audio::analysis
