// xer.audio.analysis.HarmonicContent — HarmonicContentAnalyzer binding.

#include "harmonic_content.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::HarmonicContentAnalyzer;

HarmonicWrap::HarmonicWrap(size_t fft_size) : a_(fft_size) {}
HarmonicWrap::~HarmonicWrap() = default;

HarmonicWrap* UnwrapHarmonicContentAnalyzer(v8::Local<v8::Value> v) {
    return UnwrapT<HarmonicWrap>(v);
}

void HarmonicWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new HarmonicWrap(IntArg(info, 0, 2048));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void HarmonicWrap::Analyze(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapHarmonicContentAnalyzer(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("HarmonicContent expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().Analyze(
            in.data(), in.size(), IntArg(info, 1, 44100))));
}

void HarmonicWrap::GetFundamentalFrequency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapHarmonicContentAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetFundamentalFrequency()));
}

void HarmonicWrap::GetHarmonicRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapHarmonicContentAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetHarmonicRatio()));
}

void HarmonicWrap::GetInharmonicity(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapHarmonicContentAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetInharmonicity()));
}

void HarmonicWrap::GetHarmonicSeries(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapHarmonicContentAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(F32ToJS(w->Get().GetHarmonicSeries()));
}

void HarmonicWrap::GetSummary(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapHarmonicContentAnalyzer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetSummary()).ToLocalChecked());
}

void InitAnalysisHarmonic(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "HarmonicContent", HarmonicWrap::New,
        {{"Analyze", HarmonicWrap::Analyze},
         {"GetFundamentalFrequency", HarmonicWrap::GetFundamentalFrequency},
         {"GetHarmonicRatio", HarmonicWrap::GetHarmonicRatio},
         {"GetInharmonicity", HarmonicWrap::GetInharmonicity},
         {"GetHarmonicSeries", HarmonicWrap::GetHarmonicSeries},
         {"GetSummary", HarmonicWrap::GetSummary}});
}

} // namespace xer::addon::audio::analysis
