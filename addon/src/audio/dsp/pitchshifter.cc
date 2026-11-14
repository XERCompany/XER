// xer.audio.dsp.PitchShifter — granular pitch shifting.

#include "pitchshifter.h"

#include <cmath>
#include <vector>

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::GranularPitchShifter;

PitchShifterWrap::PitchShifterWrap() = default;
PitchShifterWrap::~PitchShifterWrap() = default;

PitchShifterWrap* UnwrapPitchShifter(v8::Local<v8::Value> v) {
    return UnwrapT<PitchShifterWrap>(v);
}

void PitchShifterWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new PitchShifterWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void PitchShifterWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchShifter(info.This());
    if (!w) return;
    const float rate = static_cast<float>(NumArg(info, 0, 44100.0));
    const size_t grain = static_cast<size_t>(IntArg(info, 1, 2048));
    const size_t max_delay = static_cast<size_t>(IntArg(info, 2, 8192));
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(rate, grain, max_delay)));
}

void PitchShifterWrap::SetPitchRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchShifter(info.This());
    if (!w) return;
    w->Get().SetPitchRatio(static_cast<float>(NumArg(info, 0, 1.0)));
}

void PitchShifterWrap::SetPitchSemitones(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchShifter(info.This());
    if (!w) return;
    const double semi = NumArg(info, 0);
    w->Get().SetPitchRatio(
        static_cast<float>(std::pow(2.0, semi / 12.0)));
}

void PitchShifterWrap::GetPitchRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchShifter(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetPitchRatio()) : 1.0));
}

void PitchShifterWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchShifter(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("PitchShifter.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok =
        w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void PitchShifterWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    PitchShifterWrap::ProcessBlock(info);
}

void PitchShifterWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchShifter(info.This());
    if (w) w->Get().Reset();
}

void PitchShifterWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPitchShifter(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspPitchShifter(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "PitchShifter", PitchShifterWrap::New,
        {{"Initialize", PitchShifterWrap::Initialize},
         {"SetPitchRatio", PitchShifterWrap::SetPitchRatio},
         {"SetPitchSemitones", PitchShifterWrap::SetPitchSemitones},
         {"GetPitchRatio", PitchShifterWrap::GetPitchRatio},
         {"ProcessBlock", PitchShifterWrap::ProcessBlock},
         {"Process", PitchShifterWrap::Process},
         {"Reset", PitchShifterWrap::Reset},
         {"GetReport", PitchShifterWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
