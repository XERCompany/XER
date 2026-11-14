// xer.audio.analysis.LoudnessMeter — LoudnessMeterLUFS binding.

#include "loudness_meter.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::LoudnessMeterLUFS;

LoudnessWrap::LoudnessWrap() : a_() {}
LoudnessWrap::~LoudnessWrap() = default;

LoudnessWrap* UnwrapLoudnessMeterLUFS(v8::Local<v8::Value> v) {
    return UnwrapT<LoudnessWrap>(v);
}

void LoudnessWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new LoudnessWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void LoudnessWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("LoudnessMeter.ProcessBlock expects Float32Array");
            return;
        }
        const int channels = IntArg(info, 1, 1);
        const size_t frames =
            channels > 0 ? in.size() / static_cast<size_t>(channels) : 0;
        info.GetReturnValue().Set(Nan::New(w->Get().ProcessBlock(
            in.data(), frames, channels)));
}

void LoudnessWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    w->Get().Reset();
}

void LoudnessWrap::GetMomentaryLUFS(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetMomentaryLUFS()));
}

void LoudnessWrap::GetShortTermLUFS(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetShortTermLUFS()));
}

void LoudnessWrap::GetIntegratedLUFS(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetIntegratedLUFS()));
}

void LoudnessWrap::GetTruePeakDBFS(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetTruePeakDBFS()));
}

void LoudnessWrap::GetProcessedBlocks(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetProcessedBlocks())));
}

void LoudnessWrap::GetHistory(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    info.GetReturnValue().Set(D64ToJS(w->Get().GetHistory()));
}

void LoudnessWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLoudnessMeterLUFS(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetReport()).ToLocalChecked());
}

void InitAnalysisLoudness(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "LoudnessMeter", LoudnessWrap::New,
        {{"ProcessBlock", LoudnessWrap::ProcessBlock},
         {"Reset", LoudnessWrap::Reset},
         {"GetMomentaryLUFS", LoudnessWrap::GetMomentaryLUFS},
         {"GetShortTermLUFS", LoudnessWrap::GetShortTermLUFS},
         {"GetIntegratedLUFS", LoudnessWrap::GetIntegratedLUFS},
         {"GetTruePeakDBFS", LoudnessWrap::GetTruePeakDBFS},
         {"GetProcessedBlocks", LoudnessWrap::GetProcessedBlocks},
         {"GetHistory", LoudnessWrap::GetHistory},
         {"GetReport", LoudnessWrap::GetReport}});
}

} // namespace xer::addon::audio::analysis
