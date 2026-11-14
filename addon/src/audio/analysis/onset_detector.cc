// xer.audio.analysis.OnsetDetector — OnsetDetector binding.

#include "onset_detector.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::OnsetDetector;

OnsetWrap::OnsetWrap(size_t window_size, size_t hop_size) : a_(window_size, hop_size) {}
OnsetWrap::~OnsetWrap() = default;

OnsetWrap* UnwrapOnsetDetector(v8::Local<v8::Value> v) {
    return UnwrapT<OnsetWrap>(v);
}

void OnsetWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new OnsetWrap(IntArg(info, 0, 1024), IntArg(info, 1, 256));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void OnsetWrap::DetectOnsets(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOnsetDetector(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("OnsetDetector expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().DetectOnsets(
            in.data(), in.size(), IntArg(info, 1, 44100))));
}

void OnsetWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOnsetDetector(info.This());
    if (!w) return;
    w->Get().Reset();
}

void OnsetWrap::GetOnsetFrames(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOnsetDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(D64ToJS(w->Get().GetOnsetFrames()));
}

void OnsetWrap::GetOnsetTimesSeconds(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOnsetDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(D64ToJS(
            w->Get().GetOnsetTimesSeconds(IntArg(info, 0, 44100))));
}

void OnsetWrap::GetFluxCurve(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOnsetDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetFluxCurve()));
}

void OnsetWrap::GetAverageStrength(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOnsetDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetAverageStrength()));
}

void OnsetWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOnsetDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetReport()).ToLocalChecked());
}

void InitAnalysisOnset(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "OnsetDetector", OnsetWrap::New,
        {{"DetectOnsets", OnsetWrap::DetectOnsets},
         {"Reset", OnsetWrap::Reset},
         {"GetOnsetFrames", OnsetWrap::GetOnsetFrames},
         {"GetOnsetTimesSeconds", OnsetWrap::GetOnsetTimesSeconds},
         {"GetFluxCurve", OnsetWrap::GetFluxCurve},
         {"GetAverageStrength", OnsetWrap::GetAverageStrength},
         {"GetReport", OnsetWrap::GetReport}});
}

} // namespace xer::addon::audio::analysis
