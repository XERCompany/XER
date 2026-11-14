// xer.audio.fx.Tube — DistortionTube.

#include "tube.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::DistortionTube;

TubeWrap::TubeWrap() = default;
TubeWrap::~TubeWrap() = default;

TubeWrap* UnwrapTube(v8::Local<v8::Value> v) {
    return UnwrapT<TubeWrap>(v);
}

void TubeWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new TubeWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void TubeWrap::SetDrive(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTube(info.This());
    if (!w) return;
    w->Get().SetDrive(float(NumArg(info, 0, 1.0)));
}

void TubeWrap::SetOutputGain(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTube(info.This());
    if (!w) return;
    w->Get().SetOutputGain(float(NumArg(info, 0, 1.0)));
}

void TubeWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTube(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Tube.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void TubeWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapTube(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxTube(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Tube", TubeWrap::New,
{         {"SetDrive", TubeWrap::SetDrive},
         {"SetOutputGain", TubeWrap::SetOutputGain},
         {"ProcessBlock", TubeWrap::ProcessBlock},
         {"Process", TubeWrap::ProcessBlock},
         {"GetReport", TubeWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
