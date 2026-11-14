// xer.audio.fx.Phaser — ModPhaser.

#include "phaser.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::ModPhaser;

PhaserWrap::PhaserWrap() = default;
PhaserWrap::~PhaserWrap() = default;

PhaserWrap* UnwrapPhaser(v8::Local<v8::Value> v) {
    return UnwrapT<PhaserWrap>(v);
}

void PhaserWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new PhaserWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void PhaserWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().Initialize(float(NumArg(info, 0, 44100.0)), int(IntArg(info, 1, 4)))));
}

void PhaserWrap::SetRateHz(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    if (!w) return;
    w->Get().SetRateHz(float(NumArg(info, 0, 0.5)));
}

void PhaserWrap::SetDepth(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    if (!w) return;
    w->Get().SetDepth(float(NumArg(info, 0, 0.5)));
}

void PhaserWrap::SetFeedback(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    if (!w) return;
    w->Get().SetFeedback(float(NumArg(info, 0, 0.5)));
}

void PhaserWrap::SetMix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    if (!w) return;
    w->Get().SetMix(float(NumArg(info, 0, 0.5)));
}

void PhaserWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Phaser.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void PhaserWrap::GetMemoryStats(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetMemoryStats() : std::string()).ToLocalChecked());
}

void PhaserWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPhaser(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxPhaser(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Phaser", PhaserWrap::New,
{         {"Initialize", PhaserWrap::Initialize},
         {"SetRateHz", PhaserWrap::SetRateHz},
         {"SetDepth", PhaserWrap::SetDepth},
         {"SetFeedback", PhaserWrap::SetFeedback},
         {"SetMix", PhaserWrap::SetMix},
         {"ProcessBlock", PhaserWrap::ProcessBlock},
         {"Process", PhaserWrap::ProcessBlock},
         {"GetMemoryStats", PhaserWrap::GetMemoryStats},
         {"GetReport", PhaserWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
