// xer.audio.dsp.PhaseVocoder — phase-vocoder time stretching.

#include "phasevocoder.h"

#include <vector>

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::PhaseVocoder;

PhaseVocoderWrap::PhaseVocoderWrap(size_t fft_size, size_t hop_size)
    : pv_(fft_size, hop_size) {}
PhaseVocoderWrap::~PhaseVocoderWrap() = default;

PhaseVocoderWrap* UnwrapPhaseVocoder(v8::Local<v8::Value> v) {
    return UnwrapT<PhaseVocoderWrap>(v);
}

void PhaseVocoderWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    const size_t fft = static_cast<size_t>(IntArg(info, 0, 1024));
    const size_t hop = static_cast<size_t>(IntArg(info, 1, 256));
    auto* wrap = new PhaseVocoderWrap(fft > 0 ? fft : 1024,
                                      hop > 0 ? hop : 256);
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void PhaseVocoderWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaseVocoder(info.This());
    if (!w) return;
    const float rate = static_cast<float>(NumArg(info, 0, 44100.0));
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(rate)));
}

void PhaseVocoderWrap::SetTimeStretchRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaseVocoder(info.This());
    if (!w) return;
    w->Get().SetTimeStretchRatio(
        static_cast<float>(NumArg(info, 0, 1.0)));
}

void PhaseVocoderWrap::GetTimeStretchRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaseVocoder(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetTimeStretchRatio()) : 1.0));
}

void PhaseVocoderWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaseVocoder(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("PhaseVocoder.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok =
        w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void PhaseVocoderWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    PhaseVocoderWrap::ProcessBlock(info);
}

void PhaseVocoderWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaseVocoder(info.This());
    if (w) w->Get().Reset();
}

void PhaseVocoderWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaseVocoder(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspPhaseVocoder(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "PhaseVocoder", PhaseVocoderWrap::New,
        {{"Initialize", PhaseVocoderWrap::Initialize},
         {"SetTimeStretchRatio", PhaseVocoderWrap::SetTimeStretchRatio},
         {"GetTimeStretchRatio", PhaseVocoderWrap::GetTimeStretchRatio},
         {"ProcessBlock", PhaseVocoderWrap::ProcessBlock},
         {"Process", PhaseVocoderWrap::Process},
         {"Reset", PhaseVocoderWrap::Reset},
         {"GetReport", PhaseVocoderWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
