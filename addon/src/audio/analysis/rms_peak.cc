// xer.audio.analysis.RmsPeak — RMSPeakDetector binding.

#include "rms_peak.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::RMSPeakDetector;

RmsPeakWrap::RmsPeakWrap() : a_() {}
RmsPeakWrap::~RmsPeakWrap() = default;

RmsPeakWrap* UnwrapRMSPeakDetector(v8::Local<v8::Value> v) {
    return UnwrapT<RmsPeakWrap>(v);
}

void RmsPeakWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new RmsPeakWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void RmsPeakWrap::Analyze(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("RmsPeak.Analyze expects Float32Array");
            return;
        }
        const int channels = IntArg(info, 1, 1);
        const size_t frames =
            channels > 0 ? in.size() / static_cast<size_t>(channels) : 0;
        info.GetReturnValue().Set(Nan::New(w->Get().Analyze(
            in.data(), frames, channels)));
}

void RmsPeakWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    w->Get().Reset();
}

void RmsPeakWrap::GetRMS(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetRMS()));
}

void RmsPeakWrap::GetPeak(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetPeak()));
}

void RmsPeakWrap::GetCrestFactor(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetCrestFactor()));
}

void RmsPeakWrap::GetPeakDBFS(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetPeakDBFS()));
}

void RmsPeakWrap::GetWindowHistory(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetWindowHistory()));
}

void RmsPeakWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapRMSPeakDetector(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetReport()).ToLocalChecked());
}

void InitAnalysisRmsPeak(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "RmsPeak", RmsPeakWrap::New,
        {{"Analyze", RmsPeakWrap::Analyze},
         {"Reset", RmsPeakWrap::Reset},
         {"GetRMS", RmsPeakWrap::GetRMS},
         {"GetPeak", RmsPeakWrap::GetPeak},
         {"GetCrestFactor", RmsPeakWrap::GetCrestFactor},
         {"GetPeakDBFS", RmsPeakWrap::GetPeakDBFS},
         {"GetWindowHistory", RmsPeakWrap::GetWindowHistory},
         {"GetReport", RmsPeakWrap::GetReport}});
}

} // namespace xer::addon::audio::analysis
