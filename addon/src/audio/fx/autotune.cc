// xer.audio.fx.AutoTune — pitch correction to scale.

#include "autotune.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::AutoTuneCorrectPitch;

AutoTuneWrap::AutoTuneWrap() = default;
AutoTuneWrap::~AutoTuneWrap() = default;

AutoTuneWrap* UnwrapAutoTune(v8::Local<v8::Value> v) {
    return UnwrapT<AutoTuneWrap>(v);
}

#define XER_FX_SETTER0(Name, Method)                                     \
    void AutoTuneWrap::Name(                                           \
        const Nan::FunctionCallbackInfo<v8::Value>& info) {             \
        auto* w = UnwrapAutoTune(info.This());                          \
        if (!w) return;                                                 \
        w->Get().Method(static_cast<float>(NumArg(info, 0)));           \
    }

XER_FX_SETTER0(SetCorrectionAmount, SetCorrectionAmount)
XER_FX_SETTER0(SetCorrectionSpeed, SetCorrectionSpeed)
XER_FX_SETTER0(SetRetuneSpeedMs, SetRetuneSpeedMs)
XER_FX_SETTER0(SetHumanize, SetHumanize)
XER_FX_SETTER0(SetScaleRoot, SetScaleRoot)
XER_FX_SETTER0(SetScaleMode, SetScaleMode)
XER_FX_SETTER0(SetPresenceDb, SetPresenceDb)
XER_FX_SETTER0(SetWetMix, SetWetMix)

#undef XER_FX_SETTER0

void AutoTuneWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new AutoTuneWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void AutoTuneWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAutoTune(info.This());
    if (!w) return;
    const float rate = static_cast<float>(NumArg(info, 0, 44100.0));
    const size_t max_block = static_cast<size_t>(IntArg(info, 1, 4096));
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(rate, max_block)));
}

void AutoTuneWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAutoTune(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("AutoTune.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok = w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void AutoTuneWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AutoTuneWrap::ProcessBlock(info);
}

void AutoTuneWrap::Reset(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAutoTune(info.This());
    if (w) w->Get().Reset();
}

void AutoTuneWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAutoTune(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxAutoTune(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "AutoTune", AutoTuneWrap::New,
        {{"Initialize", AutoTuneWrap::Initialize},
         {"SetCorrectionAmount", AutoTuneWrap::SetCorrectionAmount},
         {"SetCorrectionSpeed", AutoTuneWrap::SetCorrectionSpeed},
         {"SetRetuneSpeedMs", AutoTuneWrap::SetRetuneSpeedMs},
         {"SetHumanize", AutoTuneWrap::SetHumanize},
         {"SetScaleRoot", AutoTuneWrap::SetScaleRoot},
         {"SetScaleMode", AutoTuneWrap::SetScaleMode},
         {"SetPresenceDb", AutoTuneWrap::SetPresenceDb},
         {"SetWetMix", AutoTuneWrap::SetWetMix},
         {"ProcessBlock", AutoTuneWrap::ProcessBlock},
         {"Process", AutoTuneWrap::Process},
         {"Reset", AutoTuneWrap::Reset},
         {"GetReport", AutoTuneWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
