// xer.audio.analysis.BeatTracker — BeatTracker binding.

#include "beat_tracker.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::BeatTracker;

BeatTrackerWrap::BeatTrackerWrap(size_t window_size, size_t hop_size) : a_(window_size, hop_size) {}
BeatTrackerWrap::~BeatTrackerWrap() = default;

BeatTrackerWrap* UnwrapBeatTracker(v8::Local<v8::Value> v) {
    return UnwrapT<BeatTrackerWrap>(v);
}

void BeatTrackerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new BeatTrackerWrap(IntArg(info, 0, 1024), IntArg(info, 1, 512));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void BeatTrackerWrap::Analyze(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBeatTracker(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("BeatTracker expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().Analyze(
            in.data(), in.size(), IntArg(info, 1, 44100))));
}

void BeatTrackerWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBeatTracker(info.This());
    if (!w) return;
    w->Get().Reset();
}

void BeatTrackerWrap::GetBeatEvents(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBeatTracker(info.This());
    if (!w) return;
    auto arr = Nan::New<v8::Array>(w->Get().GetBeatEvents().size());
        size_t i = 0;
        for (const auto& e : w->Get().GetBeatEvents()) {
            v8::Local<v8::Object> o = Nan::New<v8::Object>();
            Nan::Set(o, Nan::New("frame").ToLocalChecked(), Nan::New<v8::Number>(static_cast<double>(e.frame_index)));
            Nan::Set(o, Nan::New("time").ToLocalChecked(), Nan::New(e.time_seconds));
            Nan::Set(o, Nan::New("strength").ToLocalChecked(), Nan::New(e.strength));
            Nan::Set(arr, static_cast<uint32_t>(i++), o);
        }
        info.GetReturnValue().Set(arr);
}

void BeatTrackerWrap::GetEstimatedBPM(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBeatTracker(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetEstimatedBPM()));
}

void BeatTrackerWrap::GetAverageStrength(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBeatTracker(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetAverageStrength())));
}

void BeatTrackerWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBeatTracker(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetReport()).ToLocalChecked());
}

void InitAnalysisBeatTracker(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "BeatTracker", BeatTrackerWrap::New,
        {{"Analyze", BeatTrackerWrap::Analyze},
         {"Reset", BeatTrackerWrap::Reset},
         {"GetBeatEvents", BeatTrackerWrap::GetBeatEvents},
         {"GetEstimatedBPM", BeatTrackerWrap::GetEstimatedBPM},
         {"GetAverageStrength", BeatTrackerWrap::GetAverageStrength},
         {"GetReport", BeatTrackerWrap::GetReport}});
}

} // namespace xer::addon::audio::analysis
