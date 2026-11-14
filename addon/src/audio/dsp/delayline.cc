// xer.audio.dsp.DelayLine — circular delay line with fractional delay.

#include "delayline.h"

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::CircularDelayLine;

DelayLineWrap::DelayLineWrap() = default;
DelayLineWrap::~DelayLineWrap() = default;

DelayLineWrap* UnwrapDelayLine(v8::Local<v8::Value> v) {
    return UnwrapT<DelayLineWrap>(v);
}

void DelayLineWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new DelayLineWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void DelayLineWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    if (!w) return;
    const size_t max_delay =
        static_cast<size_t>(IntArg(info, 0, 44100));
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(max_delay)));
}

void DelayLineWrap::SetDelaySamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    if (!w) return;
    w->Get().SetDelaySamples(
        static_cast<size_t>(IntArg(info, 0)));
}

void DelayLineWrap::SetDelayMs(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    if (!w) return;
    const double ms = NumArg(info, 0);
    const double rate = NumArg(info, 1, 44100.0);
    w->Get().SetDelaySamples(
        static_cast<size_t>(ms * rate / 1000.0));
}

void DelayLineWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    if (!w) return;
    const float x = static_cast<float>(NumArg(info, 0));
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(w->Get().Process(x))));
}

void DelayLineWrap::ProcessFractional(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    if (!w) return;
    const float x = static_cast<float>(NumArg(info, 0));
    const float d = static_cast<float>(NumArg(info, 1));
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(w->Get().ProcessFractional(x, d))));
}

void DelayLineWrap::GetMaxDelaySamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetMaxDelaySamples()) : 0.0));
}

void DelayLineWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    if (w) w->Get().Reset();
}

void DelayLineWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDelayLine(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspDelayLine(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "DelayLine", DelayLineWrap::New,
        {{"Initialize", DelayLineWrap::Initialize},
         {"SetDelaySamples", DelayLineWrap::SetDelaySamples},
         {"SetDelayMs", DelayLineWrap::SetDelayMs},
         {"Process", DelayLineWrap::Process},
         {"ProcessFractional", DelayLineWrap::ProcessFractional},
         {"GetMaxDelaySamples", DelayLineWrap::GetMaxDelaySamples},
         {"Reset", DelayLineWrap::Reset},
         {"GetReport", DelayLineWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
