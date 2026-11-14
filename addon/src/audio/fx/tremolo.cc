// xer.audio.fx.Tremolo — ModTremolo.

#include "tremolo.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ModTremolo;

TremoloWrap::TremoloWrap() = default;
TremoloWrap::~TremoloWrap() = default;

TremoloWrap* UnwrapTremolo(v8::Local<v8::Value> v) {
    return UnwrapT<TremoloWrap>(v);
}

void TremoloWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new TremoloWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void TremoloWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTremolo(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(float(NumArg(info, 0, 44100.0)))));
}

void TremoloWrap::SetRateHz(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTremolo(info.This());
    if (!w) return;
    w->Get().SetRateHz(float(NumArg(info, 0, 5.0)));
}

void TremoloWrap::SetDepth(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTremolo(info.This());
    if (!w) return;
    w->Get().SetDepth(float(NumArg(info, 0, 0.5)));
}

void TremoloWrap::SetMix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTremolo(info.This());
    if (!w) return;
    w->Get().SetMix(float(NumArg(info, 0, 1.0)));
}

void TremoloWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTremolo(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Tremolo.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void TremoloWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTremolo(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxTremolo(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Tremolo", TremoloWrap::New,
{         {"Initialize", TremoloWrap::Initialize},
         {"SetRateHz", TremoloWrap::SetRateHz},
         {"SetDepth", TremoloWrap::SetDepth},
         {"SetMix", TremoloWrap::SetMix},
         {"ProcessBlock", TremoloWrap::ProcessBlock},
         {"Process", TremoloWrap::ProcessBlock},
         {"GetReport", TremoloWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
