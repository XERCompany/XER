// xer.audio.dsp.Biquad — biquad filter (LP/HP/BP/Notch).

#include "biquad.h"

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::BiquadProcessor;
using XER::Audio::DSP::BiquadType;

BiquadWrap::BiquadWrap() = default;
BiquadWrap::~BiquadWrap() = default;

BiquadWrap* UnwrapBiquad(v8::Local<v8::Value> v) {
    return UnwrapT<BiquadWrap>(v);
}

void BiquadWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new BiquadWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void BiquadWrap::Configure(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBiquad(info.This());
    if (!w) return;
    const auto type = EnumArg(info, 0, BiquadType::LowPass);
    const float rate = static_cast<float>(NumArg(info, 1, 44100.0));
    const float freq = static_cast<float>(NumArg(info, 2, 1000.0));
    const float q = static_cast<float>(NumArg(info, 3, 0.707f));
    info.GetReturnValue().Set(Nan::New(
        w->Get().Configure(type, rate, freq, q)));
}

void BiquadWrap::ProcessSample(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBiquad(info.This());
    if (!w) return;
    const float x = static_cast<float>(NumArg(info, 0));
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(w->Get().ProcessSample(x))));
}

void BiquadWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBiquad(info.This());
    if (w) w->Get().Reset();
}

void BiquadWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBiquad(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspBiquad(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Biquad", BiquadWrap::New,
        {{"Configure", BiquadWrap::Configure},
         {"ProcessSample", BiquadWrap::ProcessSample},
         {"Process", BiquadWrap::ProcessSample},
         {"Reset", BiquadWrap::Reset},
         {"GetReport", BiquadWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
